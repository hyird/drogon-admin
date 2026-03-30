#pragma once

#include <drogon/drogon.h>
#include "common/database/DatabaseService.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/TreeBuilder.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"
#include "SystemHelpers.hpp"
#include "SystemRequests.hpp"

using namespace drogon;

/**
 * @brief 菜单服务类
 */
class MenuService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    Task<std::vector<SystemHelpers::MenuRecordSummary>> list(const SystemRequests::MenuListQuery& query) {
        QueryBuilder qb;
        qb.notDeleted();
        if (query.keyword) {
            qb.likeAny({"name", "path", "permissionCode"}, *query.keyword);
        }
        if (query.status) {
            qb.eq("status", *query.status);
        }

        std::string sql = "SELECT * FROM sys_menu" + qb.whereClause() + " ORDER BY `order` ASC, id ASC";
        auto result = co_await dbService_.execSqlCoro(sql, qb.params());

        std::vector<SystemHelpers::MenuRecordSummary> items;
        items.reserve(result.size());
        for (const auto& row : result) {
            items.push_back(SystemHelpers::menuRecordFromRow(row));
        }
        co_return items;
    }

    Task<std::vector<TreeBuilder::TreeNode<SystemHelpers::MenuRecordSummary>>> tree(const SystemRequests::MenuTreeQuery& query) {
        QueryBuilder qb;
        qb.notDeleted();
        if (query.status) {
            qb.eq("status", *query.status);
        }

        std::string sql = "SELECT * FROM sys_menu" + qb.whereClause() + " ORDER BY `order` ASC, id ASC";
        auto result = co_await dbService_.execSqlCoro(sql, qb.params());

        std::vector<SystemHelpers::MenuRecordSummary> items;
        items.reserve(result.size());
        for (const auto& row : result) {
            items.push_back(SystemHelpers::menuRecordFromRow(row));
        }

        auto tree = TreeBuilder::build(items,
            [](const auto& item) { return item.id; },
            [](const auto& item) { return item.parentId; });
        TreeBuilder::sort(tree, [](const auto& item) { return item.order; }, true);
        co_return tree;
    }

    Task<SystemHelpers::MenuRecordSummary> detail(int id) {
        std::string sql = "SELECT * FROM sys_menu WHERE id = ? AND deletedAt IS NULL";
        auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(id)});
        if (result.empty()) throw NotFoundException("菜单不存在");
        co_return SystemHelpers::menuRecordFromRow(result[0]);
    }

    Task<void> create(const SystemRequests::MenuCreateRequest& data) {
        std::string sql = R"(
            INSERT INTO sys_menu (name, path, icon, parentId, `order`, type, component, status, permissionCode, isDefault, createdAt)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )";
        std::vector<std::string> params = {
            data.name,
            data.path.value_or(""),
            data.icon.value_or(""),
            data.parentId.has_value() ? std::to_string(*data.parentId) : "0",
            std::to_string(data.order.value_or(0)),
            data.type,
            data.component.value_or(""),
            data.status,
            data.permissionCode.value_or(""),
            data.isDefault.value_or(false) ? "1" : "0",
            TimestampHelper::now()
        };
        co_await dbService_.execSqlCoro(sql, params);

        // 清除所有用户的菜单缓存
        co_await cacheManager_.clearAllUserMenusCache();
    }

    Task<void> update(int id, const SystemRequests::MenuUpdateRequest& data) {
        co_await detail(id);
        if (data.parentId.has_value() && data.parentId->has_value() && **data.parentId == id)
            throw ValidationException("不能将菜单设为自己的子菜单");

        std::vector<std::string> setClauses, params;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) { setClauses.push_back(f + " = ?"); params.push_back(v); };

        if (data.name) addField("name", *data.name);
        if (data.path) addField("path", *data.path);
        if (data.icon) addField("icon", *data.icon);
        if (data.parentId.has_value()) {
            if (data.parentId->has_value()) {
                addField("parentId", std::to_string(**data.parentId));
            } else {
                addField("parentId", "0");
            }
        }
        if (data.order) addField("`order`", std::to_string(*data.order));
        if (data.type) addField("type", *data.type);
        if (data.component) addField("component", *data.component);
        if (data.status) addField("status", *data.status);
        if (data.permissionCode) addField("permissionCode", *data.permissionCode);
        if (data.isDefault.has_value()) addField("isDefault", *data.isDefault ? "1" : "0");

        if (setClauses.empty()) co_return;
        addField("updatedAt", TimestampHelper::now());
        params.push_back(std::to_string(id));

        std::string sql = "UPDATE sys_menu SET ";
        for (size_t i = 0; i < setClauses.size(); ++i) { if (i > 0) sql += ", "; sql += setClauses[i]; }
        sql += " WHERE id = ?";
        co_await dbService_.execSqlCoro(sql, params);

        // 清除所有用户的菜单缓存
        co_await cacheManager_.clearAllUserMenusCache();
    }

    Task<void> remove(int id) {
        auto checkSql = "SELECT COUNT(*) as count FROM sys_menu WHERE parentId = ? AND deletedAt IS NULL";
        auto checkResult = co_await dbService_.execSqlCoro(checkSql, {std::to_string(id)});
        if (!checkResult.empty() && F_INT(checkResult[0]["count"]) > 0)
            throw ValidationException("该菜单下存在子菜单，无法删除");

        co_await dbService_.execSqlCoro("DELETE FROM sys_role_menu WHERE menuId = ?", {std::to_string(id)});
        co_await dbService_.execSqlCoro("UPDATE sys_menu SET deletedAt = ? WHERE id = ?",
                                         {TimestampHelper::now(), std::to_string(id)});

        // 清除所有用户的菜单缓存
        co_await cacheManager_.clearAllUserMenusCache();
    }
};
