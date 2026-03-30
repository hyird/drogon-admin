#pragma once

#include <drogon/drogon.h>
#include "common/database/DatabaseService.hpp"
#include "common/database/TransactionGuard.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"
#include "SystemConstants.hpp"
#include "SystemHelpers.hpp"
#include "SystemRequests.hpp"

using namespace drogon;

/**
 * @brief 角色服务类
 */
class RoleService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    Task<std::tuple<std::vector<SystemHelpers::RoleListItemSummary>, int>> list(const SystemRequests::RoleListQuery& query) {
        QueryBuilder qb;
        qb.notDeleted();
        qb.ne("code", std::string(SystemConstants::SUPERADMIN_ROLE_CODE));
        if (!query.pagination.keyword.empty()) {
            qb.likeAny({"name", "code"}, query.pagination.keyword);
        }
        if (query.status) {
            qb.eq("status", *query.status);
        }

        auto countResult = co_await dbService_.execSqlCoro("SELECT COUNT(*) as count FROM sys_role" + qb.whereClause(), qb.params());
        int total = countResult.empty() ? 0 : F_INT(countResult[0]["count"]);

        auto result = co_await dbService_.execSqlCoro("SELECT * FROM sys_role" + qb.whereClause() + " ORDER BY id ASC" + query.pagination.limitClause(), qb.params());

        std::vector<SystemHelpers::RoleListItemSummary> items;
        items.reserve(result.size());
        for (const auto& row : result) {
            SystemHelpers::RoleListItemSummary item;
            item.role = SystemHelpers::roleRecordFromRow(row);
            item.menuIds = co_await getRoleMenuIds(F_INT(row["id"]));
            items.push_back(std::move(item));
        }
        co_return std::make_tuple(items, total);
    }

    Task<std::vector<SystemHelpers::RoleSummary>> all() {
        // 排除超级管理员角色，该角色只能分配给内置 admin 用户
        auto result = co_await dbService_.execSqlCoro(
            "SELECT id, name, code FROM sys_role WHERE status = 'enabled' AND deletedAt IS NULL AND code != ? ORDER BY id ASC",
            {std::string(SystemConstants::SUPERADMIN_ROLE_CODE)});
        std::vector<SystemHelpers::RoleSummary> items;
        items.reserve(result.size());
        for (const auto& row : result) {
            items.push_back(SystemHelpers::roleSummaryFromRow(row));
        }
        co_return items;
    }

    Task<SystemHelpers::RoleDetailSummary> detail(int id) {
        auto result = co_await dbService_.execSqlCoro("SELECT * FROM sys_role WHERE id = ? AND deletedAt IS NULL", {std::to_string(id)});
        if (result.empty()) throw NotFoundException("角色不存在");
        SystemHelpers::RoleDetailSummary item;
        item.role = SystemHelpers::roleRecordFromRow(result[0]);
        item.menuIds = co_await getRoleMenuIds(id);
        item.menus = co_await getRoleMenus(id);
        co_return item;
    }

    Task<void> create(const SystemRequests::RoleCreateRequest& data) {
        if (!data.code.empty()) co_await checkCodeUnique(data.code);

        auto tx = co_await TransactionGuard::create(dbService_);

        co_await tx.execSqlCoro(
            "INSERT INTO sys_role (name, code, description, status, createdAt) VALUES (?, ?, ?, ?, ?)",
            {
                data.name,
                data.code,
                data.description.value_or(""),
                data.status,
                TimestampHelper::now()
            }
        );

        auto lastIdResult = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
        int roleId = F_INT(lastIdResult[0]["id"]);

        if (data.menuIds) {
            co_await syncMenus(tx, roleId, *data.menuIds);
        }

        // 提交成功后清除所有用户的角色和菜单缓存
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.clearAllUserRolesCache();
            co_await cacheManager_.clearAllUserMenusCache();
        });

        co_await tx.commit();
    }

    Task<void> update(int id, const SystemRequests::RoleUpdateRequest& data) {
        auto role = co_await detail(id);
        if (SystemHelpers::isRoleCode(role.role, SystemConstants::SUPERADMIN_ROLE_CODE))
            throw ForbiddenException("不能编辑超级管理员角色");
        if (data.code && !data.code->empty()) {
            co_await checkCodeUnique(*data.code, id);
        }

        auto tx = co_await TransactionGuard::create(dbService_);

        std::vector<std::string> setClauses, params;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) { setClauses.push_back(f + " = ?"); params.push_back(v); };

        if (data.name) addField("name", *data.name);
        if (data.code) addField("code", *data.code);
        if (data.description) addField("description", *data.description);
        if (data.status) addField("status", *data.status);

        if (!setClauses.empty()) {
            addField("updatedAt", TimestampHelper::now());
            params.push_back(std::to_string(id));
            std::string sql = "UPDATE sys_role SET ";
            for (size_t i = 0; i < setClauses.size(); ++i) { if (i > 0) sql += ", "; sql += setClauses[i]; }
            sql += " WHERE id = ?";
            co_await tx.execSqlCoro(sql, params);
        }

        if (data.menuIds) {
            co_await syncMenus(tx, id, *data.menuIds);
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
        if (SystemHelpers::isRoleCode(role.role, SystemConstants::SUPERADMIN_ROLE_CODE))
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

    Task<std::vector<int>> getRoleMenuIds(int roleId) {
        auto result = co_await dbService_.execSqlCoro("SELECT menuId FROM sys_role_menu WHERE roleId = ?", {std::to_string(roleId)});
        std::vector<int> menuIds;
        menuIds.reserve(result.size());
        for (const auto& row : result) menuIds.push_back(F_INT(row["menuId"]));
        co_return menuIds;
    }

    Task<std::vector<SystemHelpers::RoleMenuSummary>> getRoleMenus(int roleId) {
        auto result = co_await dbService_.execSqlCoro(
            "SELECT m.id, m.name, m.type, m.parentId FROM sys_menu m INNER JOIN sys_role_menu rm ON m.id = rm.menuId WHERE rm.roleId = ? AND m.deletedAt IS NULL ORDER BY m.`order` ASC",
            {std::to_string(roleId)});
        std::vector<SystemHelpers::RoleMenuSummary> menus;
        menus.reserve(result.size());
        for (const auto& row : result) {
            menus.push_back(SystemHelpers::roleMenuSummaryFromRow(row));
        }
        co_return menus;
    }

    Task<void> syncMenus(TransactionGuard& tx, int roleId, const std::vector<int>& menuIds) {
        co_await tx.execSqlCoro("DELETE FROM sys_role_menu WHERE roleId = ?", {std::to_string(roleId)});

        if (!menuIds.empty()) {
            std::string sql = "INSERT INTO sys_role_menu (roleId, menuId) VALUES ";
            std::vector<std::string> valueParts, params;
            for (int menuId : menuIds) {
                valueParts.push_back("(?, ?)");
                params.push_back(std::to_string(roleId));
                params.push_back(std::to_string(menuId));
            }
            sql += valueParts[0];
            for (size_t i = 1; i < valueParts.size(); ++i) sql += ", " + valueParts[i];
            co_await tx.execSqlCoro(sql, params);
        }
    }
};
