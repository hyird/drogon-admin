#pragma once

#include <algorithm>
#include <optional>
#include <string_view>

#include <drogon/drogon.h>
#include "common/database/DatabaseService.hpp"
#include "common/database/TransactionGuard.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/TreeBuilder.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"
#include "SystemHelpers.hpp"
#include "SystemEntityGuard.hpp"
#include "SystemDataLoader.hpp"
#include "SystemStringUtils.hpp"
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
        auto records = co_await SystemDataLoader::loadMenuRecords(dbService_, cacheManager_);
        co_return filterMenuRecords(records, query.status, query.keyword);
    }

    Task<std::vector<TreeBuilder::TreeNode<SystemHelpers::MenuRecordSummary>>> tree(const SystemRequests::MenuTreeQuery& query) {
        auto records = co_await SystemDataLoader::loadMenuRecords(dbService_, cacheManager_);
        auto items = filterMenuRecords(records, query.status, std::nullopt);

        auto tree = TreeBuilder::build(items,
            [](const auto& item) { return item.id; },
            [](const auto& item) { return item.parentId; });
        TreeBuilder::sort(tree, [](const auto& item) { return item.order; }, true);
        co_return tree;
    }

    Task<SystemHelpers::MenuRecordSummary> detail(int id) {
        auto records = co_await SystemDataLoader::loadMenuRecords(dbService_, cacheManager_);
        for (const auto& record : records) {
            if (record.id == id) {
                co_return record;
            }
        }

        throw NotFoundException("菜单不存在");
    }

    Task<void> create(const SystemRequests::MenuCreateRequest& data) {
        auto tx = co_await TransactionGuard::create(dbService_);
        if (data.parentId.has_value()) {
            co_await SystemEntityGuard::lockEnabledRowForUpdate(tx, "sys_menu", *data.parentId, "父菜单不存在或已禁用");
        }

        std::string sql = R"(
            INSERT INTO sys_menu (name, path, icon, parentId, `order`, type, component, status, permissionCode, isDefault, createdAt)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )";
        std::vector<std::optional<std::string>> params = {
            std::optional<std::string>{data.name},
            std::optional<std::string>{data.path.value_or("")},
            std::optional<std::string>{data.icon.value_or("")},
            data.parentId.has_value()
                ? std::optional<std::string>{std::to_string(*data.parentId)}
                : std::nullopt,
            std::optional<std::string>{std::to_string(data.order.value_or(0))},
            std::optional<std::string>{data.type},
            std::optional<std::string>{data.component.value_or("")},
            std::optional<std::string>{data.status},
            std::optional<std::string>{data.permissionCode.value_or("")},
            std::optional<std::string>{data.isDefault.value_or(false) ? "1" : "0"},
            std::optional<std::string>{TimestampHelper::now()}
        };
        co_await tx.execSqlCoroNullable(sql, params);

        // 提交成功后使授权相关缓存失效
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateAuthorizationCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

    Task<void> update(int id, const SystemRequests::MenuUpdateRequest& data) {
        auto tx = co_await TransactionGuard::create(dbService_);
        co_await SystemEntityGuard::lockRowForUpdate(tx, "sys_menu", id, "菜单不存在");
        if (data.parentId.has_value() && data.parentId->has_value() && **data.parentId == id)
            throw ValidationException("不能将菜单设为自己的子菜单");

        if (data.parentId.has_value() && data.parentId->has_value()) {
            co_await SystemEntityGuard::lockEnabledRowForUpdate(tx, "sys_menu", **data.parentId, "父菜单不存在或已禁用");
        }

        std::vector<std::optional<std::string>> params;
        std::vector<std::string> setClauses;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) {
            setClauses.push_back(f + " = ?");
            params.emplace_back(v);
        };

        if (data.name) addField("name", *data.name);
        if (data.path) addField("path", *data.path);
        if (data.icon) addField("icon", *data.icon);
        if (data.parentId.has_value()) {
            setClauses.push_back("parentId = ?");
            if (data.parentId->has_value()) {
                params.emplace_back(std::to_string(**data.parentId));
            } else {
                params.emplace_back(std::nullopt);
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
        co_await tx.execSqlCoroNullable(sql, params);

        // 提交成功后使授权相关缓存失效
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateAuthorizationCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

    Task<void> remove(int id) {
        auto tx = co_await TransactionGuard::create(dbService_);
        co_await SystemEntityGuard::lockRowForUpdate(tx, "sys_menu", id, "菜单不存在");

        if (co_await SystemEntityGuard::hasChildMenu(tx, id))
            throw ValidationException("该菜单下存在子菜单，无法删除");

        co_await tx.execSqlCoro("DELETE FROM sys_role_menu WHERE menuId = ?", {std::to_string(id)});
        co_await tx.execSqlCoro("UPDATE sys_menu SET deletedAt = ? WHERE id = ?",
                                {TimestampHelper::now(), std::to_string(id)});

        // 提交成功后使授权相关缓存失效
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateAuthorizationCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

private:
    static std::vector<SystemHelpers::MenuRecordSummary> filterMenuRecords(
        const std::vector<SystemHelpers::MenuRecordSummary>& records,
        const std::optional<std::string>& status,
        const std::optional<std::string>& keyword) {
        std::vector<SystemHelpers::MenuRecordSummary> items;
        items.reserve(records.size());

        for (const auto& record : records) {
            if (status && record.status != *status) {
                continue;
            }

            if (keyword) {
                if (!SystemStringUtils::containsIgnoreCase(record.name, *keyword) &&
                    !SystemStringUtils::containsIgnoreCase(record.path, *keyword) &&
                    !SystemStringUtils::containsIgnoreCase(record.permissionCode, *keyword)) {
                    continue;
                }
            }

            items.push_back(record);
        }

        return items;
    }

};
