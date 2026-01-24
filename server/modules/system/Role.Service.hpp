#pragma once

#include <drogon/drogon.h>
#include "common/database/DatabaseService.hpp"
#include "common/database/TransactionGuard.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"

using namespace drogon;

/**
 * @brief 角色服务类
 */
class RoleService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    Task<std::tuple<Json::Value, int>> list(const Pagination& page, const std::string& status = "") {
        QueryBuilder qb;
        qb.notDeleted();
        if (!page.keyword.empty()) qb.likeAny({"name", "code"}, page.keyword);
        if (!status.empty()) qb.eq("status", status);

        auto countResult = co_await dbService_.execSqlCoro("SELECT COUNT(*) as count FROM sys_role" + qb.whereClause(), qb.params());
        int total = countResult.empty() ? 0 : F_INT(countResult[0]["count"]);

        auto result = co_await dbService_.execSqlCoro("SELECT * FROM sys_role" + qb.whereClause() + " ORDER BY id ASC" + page.limitClause(), qb.params());

        Json::Value items(Json::arrayValue);
        for (const auto& row : result) {
            auto item = rowToJson(row);
            item["menuIds"] = co_await getRoleMenuIds(F_INT(row["id"]));
            items.append(item);
        }
        co_return std::make_tuple(items, total);
    }

    Task<Json::Value> all() {
        // 排除超级管理员角色，该角色只能分配给内置 admin 用户
        auto result = co_await dbService_.execSqlCoro("SELECT id, name, code FROM sys_role WHERE status = 'enabled' AND deletedAt IS NULL AND code != 'superadmin' ORDER BY id ASC");
        Json::Value items(Json::arrayValue);
        for (const auto& row : result) {
            Json::Value item;
            item["id"] = F_INT(row["id"]);
            item["name"] = F_STR(row["name"]);
            item["code"] = F_STR(row["code"]);
            items.append(item);
        }
        co_return items;
    }

    Task<Json::Value> detail(int id) {
        auto result = co_await dbService_.execSqlCoro("SELECT * FROM sys_role WHERE id = ? AND deletedAt IS NULL", {std::to_string(id)});
        if (result.empty()) throw NotFoundException("角色不存在");
        auto item = rowToJson(result[0]);
        item["menuIds"] = co_await getRoleMenuIds(id);
        item["menus"] = co_await getRoleMenus(id);
        co_return item;
    }

    Task<void> create(const Json::Value& data) {
        std::string code = data.get("code", "").asString();
        if (!code.empty()) co_await checkCodeUnique(code);

        auto tx = co_await TransactionGuard::create(dbService_);

        co_await tx.execSqlCoro(
            "INSERT INTO sys_role (name, code, description, status, createdAt) VALUES (?, ?, ?, ?, ?)",
            {
                data.get("name", "").asString(),
                code,
                data.get("description", "").asString(),
                data.get("status", "enabled").asString(),
                TimestampHelper::now()
            }
        );

        auto lastIdResult = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
        int roleId = F_INT(lastIdResult[0]["id"]);

        if (data.isMember("menuIds") && data["menuIds"].isArray()) {
            co_await syncMenus(tx, roleId, data["menuIds"]);
        }

        // 提交成功后清除所有用户的角色和菜单缓存
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.clearAllUserRolesCache();
            co_await cacheManager_.clearAllUserMenusCache();
        });

        co_await tx.commit();
    }

    Task<void> update(int id, const Json::Value& data) {
        auto role = co_await detail(id);
        if (role["code"].asString() == "superadmin")
            throw ForbiddenException("不能编辑超级管理员角色");
        if (data.isMember("code") && !data["code"].isNull()) {
            std::string code = data["code"].asString();
            if (!code.empty()) co_await checkCodeUnique(code, id);
        }

        auto tx = co_await TransactionGuard::create(dbService_);

        std::vector<std::string> setClauses, params;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) { setClauses.push_back(f + " = ?"); params.push_back(v); };

        if (data.isMember("name")) addField("name", data["name"].asString());
        if (data.isMember("code")) addField("code", data["code"].asString());
        if (data.isMember("description")) addField("description", data["description"].asString());
        if (data.isMember("status")) addField("status", data["status"].asString());

        if (!setClauses.empty()) {
            addField("updatedAt", TimestampHelper::now());
            params.push_back(std::to_string(id));
            std::string sql = "UPDATE sys_role SET ";
            for (size_t i = 0; i < setClauses.size(); ++i) { if (i > 0) sql += ", "; sql += setClauses[i]; }
            sql += " WHERE id = ?";
            co_await tx.execSqlCoro(sql, params);
        }

        if (data.isMember("menuIds") && data["menuIds"].isArray()) {
            co_await syncMenus(tx, id, data["menuIds"]);
        }

        // 提交成功后清除所有用户的角色和菜单缓存
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.clearAllUserRolesCache();
            co_await cacheManager_.clearAllUserMenusCache();
        });

        co_await tx.commit();
    }

    Task<void> remove(int id) {
        auto role = co_await detail(id);
        if (role["code"].asString() == "superadmin")
            throw ForbiddenException("不能删除超级管理员角色");

        auto checkResult = co_await dbService_.execSqlCoro("SELECT COUNT(*) as count FROM sys_user_role WHERE roleId = ?", {std::to_string(id)});
        if (!checkResult.empty() && F_INT(checkResult[0]["count"]) > 0)
            throw ValidationException("该角色已被用户使用，无法删除");

        auto tx = co_await TransactionGuard::create(dbService_);

        co_await tx.execSqlCoro("DELETE FROM sys_role_menu WHERE roleId = ?", {std::to_string(id)});
        co_await tx.execSqlCoro("UPDATE sys_role SET deletedAt = ? WHERE id = ?", {TimestampHelper::now(), std::to_string(id)});

        // 提交成功后清除所有用户的角色和菜单缓存
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.clearAllUserRolesCache();
            co_await cacheManager_.clearAllUserMenusCache();
        });

        co_await tx.commit();
    }

private:
    Task<void> checkCodeUnique(const std::string& code, int excludeId = 0) {
        std::string sql = "SELECT id FROM sys_role WHERE code = ? AND deletedAt IS NULL";
        std::vector<std::string> params = {code};
        if (excludeId > 0) { sql += " AND id != ?"; params.push_back(std::to_string(excludeId)); }
        auto result = co_await dbService_.execSqlCoro(sql, params);
        if (!result.empty()) throw ValidationException("角色编码已存在");
    }

    Task<Json::Value> getRoleMenuIds(int roleId) {
        auto result = co_await dbService_.execSqlCoro("SELECT menuId FROM sys_role_menu WHERE roleId = ?", {std::to_string(roleId)});
        Json::Value menuIds(Json::arrayValue);
        for (const auto& row : result) menuIds.append(F_INT(row["menuId"]));
        co_return menuIds;
    }

    Task<Json::Value> getRoleMenus(int roleId) {
        auto result = co_await dbService_.execSqlCoro(
            "SELECT m.id, m.name, m.type, m.parentId FROM sys_menu m INNER JOIN sys_role_menu rm ON m.id = rm.menuId WHERE rm.roleId = ? AND m.deletedAt IS NULL ORDER BY m.`order` ASC",
            {std::to_string(roleId)});
        Json::Value menus(Json::arrayValue);
        for (const auto& row : result) {
            Json::Value menu;
            menu["id"] = F_INT(row["id"]);
            menu["name"] = F_STR(row["name"]);
            menu["type"] = F_STR(row["type"]);
            menu["parentId"] = F_INT_DEF(row["parentId"], 0);
            menus.append(menu);
        }
        co_return menus;
    }

    Task<void> syncMenus(TransactionGuard& tx, int roleId, const Json::Value& menuIds) {
        co_await tx.execSqlCoro("DELETE FROM sys_role_menu WHERE roleId = ?", {std::to_string(roleId)});

        if (menuIds.isArray() && !menuIds.empty()) {
            std::string sql = "INSERT INTO sys_role_menu (roleId, menuId) VALUES ";
            std::vector<std::string> valueParts, params;
            for (const auto& menuId : menuIds) {
                valueParts.push_back("(?, ?)");
                params.push_back(std::to_string(roleId));
                params.push_back(std::to_string(menuId.asInt()));
            }
            sql += valueParts[0];
            for (size_t i = 1; i < valueParts.size(); ++i) sql += ", " + valueParts[i];
            co_await tx.execSqlCoro(sql, params);
        }
    }

    Json::Value rowToJson(const drogon::orm::Row& row) {
        Json::Value json;
        json["id"] = F_INT(row["id"]);
        json["name"] = F_STR(row["name"]);
        json["code"] = F_STR(row["code"]);
        json["description"] = F_STR_DEF(row["description"], "");
        json["status"] = F_STR_DEF(row["status"], "enabled");
        json["createdAt"] = F_STR_DEF(row["createdAt"], "");
        json["updatedAt"] = F_STR_DEF(row["updatedAt"], "");
        return json;
    }
};
