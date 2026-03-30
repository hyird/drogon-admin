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
#include "SystemHelpers.hpp"
#include "SystemConstants.hpp"
#include "SystemRequests.hpp"

using namespace drogon;

/**
 * @brief 用户服务类
 */
class UserService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    Task<std::tuple<std::vector<SystemHelpers::UserListItemSummary>, int>> list(const SystemRequests::UserListQuery& query) {
        QueryBuilder qb;
        qb.notDeleted("u.deletedAt");
        if (!query.pagination.keyword.empty()) {
            qb.likeAny({"u.username", "u.nickname", "u.phone", "u.email"}, query.pagination.keyword);
        }
        if (query.status) {
            qb.eq("u.status", *query.status);
        }
        if (query.departmentId) {
            qb.eq("u.departmentId", std::to_string(*query.departmentId));
        }

        auto countResult = co_await dbService_.execSqlCoro("SELECT COUNT(*) as count FROM sys_user u" + qb.whereClause(), qb.params());
        int total = countResult.empty() ? 0 : F_INT(countResult[0]["count"]);

        std::string sql = "SELECT u.*, d.name as departmentName FROM sys_user u LEFT JOIN sys_department d ON u.departmentId = d.id AND d.deletedAt IS NULL" + qb.whereClause() + " ORDER BY u.id ASC" + query.pagination.limitClause();
        auto result = co_await dbService_.execSqlCoro(sql, qb.params());

        std::vector<SystemHelpers::UserListItemSummary> items;
        items.reserve(result.size());
        for (const auto& row : result) {
            SystemHelpers::UserListItemSummary item;
            item.user = SystemHelpers::userRecordFromRow(row);
            item.roles = co_await getUserRoles(F_INT(row["id"]));
            items.push_back(std::move(item));
        }
        co_return std::make_tuple(items, total);
    }

    Task<SystemHelpers::UserDetailSummary> detail(int id) {
        std::string sql = "SELECT u.*, d.name as departmentName FROM sys_user u LEFT JOIN sys_department d ON u.departmentId = d.id AND d.deletedAt IS NULL WHERE u.id = ? AND u.deletedAt IS NULL";
        auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(id)});
        if (result.empty()) throw NotFoundException("用户不存在");
        SystemHelpers::UserDetailSummary item;
        item.user = SystemHelpers::userRecordFromRow(result[0]);
        item.roles = co_await getUserRoles(id);
        item.roleIds = co_await getUserRoleIds(id);
        co_return item;
    }

    Task<void> create(const SystemRequests::UserCreateRequest& data) {
        const std::string& username = data.username;
        co_await checkUsernameUnique(username);

        auto tx = co_await TransactionGuard::create(dbService_);
        std::string passwordHash = PasswordUtils::hashPassword(data.password);

        co_await tx.execSqlCoro(
            "INSERT INTO sys_user (username, passwordHash, nickname, phone, email, departmentId, status, createdAt) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {
                username, passwordHash, data.nickname.value_or(""),
                data.phone.value_or(""), data.email.value_or(""),
                data.departmentId.has_value() ? std::to_string(*data.departmentId) : "0",
                data.status, TimestampHelper::now()
            }
        );

        auto lastIdResult = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
        int userId = F_INT(lastIdResult[0]["id"]);

        if (!data.roleIds.empty()) {
            co_await syncRoles(tx, userId, data.roleIds);
        }

        // 提交成功后清除缓存
        tx.onCommit([this, userId]() -> Task<void> {
            co_await cacheManager_.clearUserCache(userId);
        });

        co_await tx.commit();
    }

    Task<void> update(int id, const SystemRequests::UserUpdateRequest& data) {
        co_await detail(id);
        co_await checkNotBuiltinAdmin(id);

        auto tx = co_await TransactionGuard::create(dbService_);

        std::vector<std::string> setClauses, params;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) { setClauses.push_back(f + " = ?"); params.push_back(v); };

        if (data.nickname) addField("nickname", *data.nickname);
        if (data.phone) addField("phone", *data.phone);
        if (data.email) addField("email", *data.email);
        if (data.departmentId.has_value()) {
            if (data.departmentId->has_value()) {
                addField("departmentId", std::to_string(**data.departmentId));
            } else {
                addField("departmentId", "0");
            }
        }
        if (data.status) addField("status", *data.status);
        if (data.password) addField("passwordHash", PasswordUtils::hashPassword(*data.password));

        if (!setClauses.empty()) {
            addField("updatedAt", TimestampHelper::now());
            params.push_back(std::to_string(id));
            std::string sql = "UPDATE sys_user SET ";
            for (size_t i = 0; i < setClauses.size(); ++i) { if (i > 0) sql += ", "; sql += setClauses[i]; }
            sql += " WHERE id = ?";
            co_await tx.execSqlCoro(sql, params);
        }

        if (data.roleIds) {
            co_await syncRoles(tx, id, *data.roleIds);
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
            "SELECT id FROM sys_user WHERE id = ? AND username = ? AND deletedAt IS NULL",
            {std::to_string(userId), std::string(SystemConstants::DEFAULT_ADMIN_USERNAME)});
        if (!result.empty()) throw ForbiddenException("不能编辑或删除内建管理员账户");
    }

    Task<std::vector<SystemHelpers::RoleSummary>> getUserRoles(int userId) {
        auto result = co_await dbService_.execSqlCoro(
            "SELECT r.id, r.name, r.code FROM sys_role r INNER JOIN sys_user_role ur ON r.id = ur.roleId WHERE ur.userId = ? AND r.deletedAt IS NULL",
            {std::to_string(userId)});
        std::vector<SystemHelpers::RoleSummary> roles;
        roles.reserve(result.size());
        for (const auto& row : result) {
            roles.push_back(SystemHelpers::roleSummaryFromRow(row));
        }
        co_return roles;
    }

    Task<std::vector<int>> getUserRoleIds(int userId) {
        auto result = co_await dbService_.execSqlCoro("SELECT roleId FROM sys_user_role WHERE userId = ?", {std::to_string(userId)});
        std::vector<int> roleIds;
        roleIds.reserve(result.size());
        for (const auto& row : result) roleIds.push_back(F_INT(row["roleId"]));
        co_return roleIds;
    }

    Task<void> syncRoles(TransactionGuard& tx, int userId, const std::vector<int>& roleIds) {
        co_await tx.execSqlCoro("DELETE FROM sys_user_role WHERE userId = ?", {std::to_string(userId)});

        if (!roleIds.empty()) {
            std::string sql = "INSERT INTO sys_user_role (userId, roleId) VALUES ";
            std::vector<std::string> valueParts, params;
            for (int roleId : roleIds) {
                valueParts.push_back("(?, ?)");
                params.push_back(std::to_string(userId));
                params.push_back(std::to_string(roleId));
            }
            sql += valueParts[0];
            for (size_t i = 1; i < valueParts.size(); ++i) sql += ", " + valueParts[i];
            co_await tx.execSqlCoro(sql, params);
        }
    }
};
