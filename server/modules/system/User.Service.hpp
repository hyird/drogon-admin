#pragma once

#include <drogon/drogon.h>
#include "common/database/DatabaseService.hpp"
#include "common/database/TransactionGuard.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/PasswordUtils.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"

using namespace drogon;

/**
 * @brief 用户服务类
 */
class UserService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    Task<std::tuple<Json::Value, int>> list(const Pagination& page, const std::string& status = "", int departmentId = 0) {
        QueryBuilder qb;
        qb.notDeleted("u.deletedAt");
        if (!page.keyword.empty()) qb.likeAny({"u.username", "u.nickname", "u.phone", "u.email"}, page.keyword);
        if (!status.empty()) qb.eq("u.status", status);
        if (departmentId > 0) qb.eq("u.departmentId", std::to_string(departmentId));

        auto countResult = co_await dbService_.execSqlCoro("SELECT COUNT(*) as count FROM sys_user u" + qb.whereClause(), qb.params());
        int total = countResult.empty() ? 0 : F_INT(countResult[0]["count"]);

        std::string sql = "SELECT u.*, d.name as departmentName FROM sys_user u LEFT JOIN sys_department d ON u.departmentId = d.id AND d.deletedAt IS NULL" + qb.whereClause() + " ORDER BY u.id ASC" + page.limitClause();
        auto result = co_await dbService_.execSqlCoro(sql, qb.params());

        Json::Value items(Json::arrayValue);
        for (const auto& row : result) {
            auto item = rowToJson(row);
            item["roles"] = co_await getUserRoles(F_INT(row["id"]));
            items.append(item);
        }
        co_return std::make_tuple(items, total);
    }

    Task<Json::Value> detail(int id) {
        std::string sql = "SELECT u.*, d.name as departmentName FROM sys_user u LEFT JOIN sys_department d ON u.departmentId = d.id AND d.deletedAt IS NULL WHERE u.id = ? AND u.deletedAt IS NULL";
        auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(id)});
        if (result.empty()) throw NotFoundException("用户不存在");
        auto item = rowToJson(result[0]);
        item["roles"] = co_await getUserRoles(id);
        item["roleIds"] = co_await getUserRoleIds(id);
        co_return item;
    }

    Task<void> create(const Json::Value& data) {
        std::string username = data.get("username", "").asString();
        co_await checkUsernameUnique(username);

        auto tx = co_await TransactionGuard::create(dbService_);
        std::string passwordHash = PasswordUtils::hashPassword(data.get("password", "").asString());

        co_await tx.execSqlCoro(
            "INSERT INTO sys_user (username, passwordHash, nickname, phone, email, departmentId, status, createdAt) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {
                username, passwordHash, data.get("nickname", "").asString(),
                data.get("phone", "").asString(), data.get("email", "").asString(),
                data.get("departmentId", 0).isNull() ? "0" : std::to_string(data["departmentId"].asInt()),
                data.get("status", "enabled").asString(), TimestampHelper::now()
            }
        );

        auto lastIdResult = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
        int userId = F_INT(lastIdResult[0]["id"]);

        if (data.isMember("roleIds") && data["roleIds"].isArray()) {
            co_await syncRoles(tx, userId, data["roleIds"]);
        }

        // 提交成功后清除缓存
        tx.onCommit([this, userId]() -> Task<void> {
            co_await cacheManager_.clearUserCache(userId);
        });

        co_await tx.commit();
    }

    Task<void> update(int id, const Json::Value& data) {
        co_await detail(id);
        co_await checkNotBuiltinAdmin(id);

        auto tx = co_await TransactionGuard::create(dbService_);

        std::vector<std::string> setClauses, params;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) { setClauses.push_back(f + " = ?"); params.push_back(v); };

        if (data.isMember("nickname")) addField("nickname", data["nickname"].asString());
        if (data.isMember("phone")) addField("phone", data["phone"].asString());
        if (data.isMember("email")) addField("email", data["email"].asString());
        if (data.isMember("departmentId")) addField("departmentId", data["departmentId"].isNull() ? "0" : std::to_string(data["departmentId"].asInt()));
        if (data.isMember("status")) addField("status", data["status"].asString());
        if (data.isMember("password") && !data["password"].asString().empty())
            addField("passwordHash", PasswordUtils::hashPassword(data["password"].asString()));

        if (!setClauses.empty()) {
            addField("updatedAt", TimestampHelper::now());
            params.push_back(std::to_string(id));
            std::string sql = "UPDATE sys_user SET ";
            for (size_t i = 0; i < setClauses.size(); ++i) { if (i > 0) sql += ", "; sql += setClauses[i]; }
            sql += " WHERE id = ?";
            co_await tx.execSqlCoro(sql, params);
        }

        if (data.isMember("roleIds") && data["roleIds"].isArray()) {
            co_await syncRoles(tx, id, data["roleIds"]);
        }

        // 提交成功后清除用户缓存
        tx.onCommit([this, id]() -> Task<void> {
            co_await cacheManager_.clearUserCache(id);
        });

        co_await tx.commit();
    }

    Task<void> remove(int id) {
        co_await checkNotBuiltinAdmin(id);

        auto tx = co_await TransactionGuard::create(dbService_);

        co_await tx.execSqlCoro("DELETE FROM sys_user_role WHERE userId = ?", {std::to_string(id)});
        co_await tx.execSqlCoro("UPDATE sys_user SET deletedAt = ? WHERE id = ?", {TimestampHelper::now(), std::to_string(id)});

        // 提交成功后清除用户缓存
        tx.onCommit([this, id]() -> Task<void> {
            co_await cacheManager_.clearUserCache(id);
        });

        co_await tx.commit();
    }

private:
    Task<void> checkUsernameUnique(const std::string& username, int excludeId = 0) {
        std::string sql = "SELECT id FROM sys_user WHERE username = ? AND deletedAt IS NULL";
        std::vector<std::string> params = {username};
        if (excludeId > 0) { sql += " AND id != ?"; params.push_back(std::to_string(excludeId)); }
        auto result = co_await dbService_.execSqlCoro(sql, params);
        if (!result.empty()) throw ValidationException("用户名已存在");
    }

    Task<void> checkNotBuiltinAdmin(int userId) {
        auto result = co_await dbService_.execSqlCoro(
            "SELECT id FROM sys_user WHERE id = ? AND username = 'admin' AND deletedAt IS NULL",
            {std::to_string(userId)});
        if (!result.empty()) throw ForbiddenException("不能编辑或删除内建管理员账户");
    }

    Task<Json::Value> getUserRoles(int userId) {
        auto result = co_await dbService_.execSqlCoro(
            "SELECT r.id, r.name, r.code FROM sys_role r INNER JOIN sys_user_role ur ON r.id = ur.roleId WHERE ur.userId = ? AND r.deletedAt IS NULL",
            {std::to_string(userId)});
        Json::Value roles(Json::arrayValue);
        for (const auto& row : result) {
            Json::Value role;
            role["id"] = F_INT(row["id"]);
            role["name"] = F_STR(row["name"]);
            role["code"] = F_STR(row["code"]);
            roles.append(role);
        }
        co_return roles;
    }

    Task<Json::Value> getUserRoleIds(int userId) {
        auto result = co_await dbService_.execSqlCoro("SELECT roleId FROM sys_user_role WHERE userId = ?", {std::to_string(userId)});
        Json::Value roleIds(Json::arrayValue);
        for (const auto& row : result) roleIds.append(F_INT(row["roleId"]));
        co_return roleIds;
    }

    Task<void> syncRoles(TransactionGuard& tx, int userId, const Json::Value& roleIds) {
        co_await tx.execSqlCoro("DELETE FROM sys_user_role WHERE userId = ?", {std::to_string(userId)});

        if (roleIds.isArray() && !roleIds.empty()) {
            std::string sql = "INSERT INTO sys_user_role (userId, roleId) VALUES ";
            std::vector<std::string> valueParts, params;
            for (const auto& roleId : roleIds) {
                valueParts.push_back("(?, ?)");
                params.push_back(std::to_string(userId));
                params.push_back(std::to_string(roleId.asInt()));
            }
            sql += valueParts[0];
            for (size_t i = 1; i < valueParts.size(); ++i) sql += ", " + valueParts[i];
            co_await tx.execSqlCoro(sql, params);
        }
    }

    Json::Value rowToJson(const drogon::orm::Row& row) {
        Json::Value json;
        json["id"] = F_INT(row["id"]);
        json["username"] = F_STR(row["username"]);
        json["nickname"] = F_STR_DEF(row["nickname"], "");
        json["phone"] = F_STR_DEF(row["phone"], "");
        json["email"] = F_STR_DEF(row["email"], "");
        json["departmentId"] = F_INT_DEF(row["departmentId"], 0);
        try { json["departmentName"] = F_STR_DEF(row["departmentName"], ""); } catch (...) { json["departmentName"] = ""; }
        json["status"] = F_STR_DEF(row["status"], "enabled");
        json["createdAt"] = F_STR_DEF(row["createdAt"], "");
        json["updatedAt"] = F_STR_DEF(row["updatedAt"], "");
        return json;
    }
};
