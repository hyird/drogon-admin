#pragma once

#include <algorithm>
#include <drogon/drogon.h>
#include <unordered_map>

#include "common/database/DatabaseService.hpp"
#include "common/database/TransactionGuard.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"
#include "SystemConstants.hpp"
#include "SystemHelpers.hpp"
#include "SystemEntityGuard.hpp"
#include "SystemDataLoader.hpp"
#include "SystemRelationLoader.hpp"
#include "SystemStringUtils.hpp"
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
        auto records = co_await SystemDataLoader::loadRoleRecords(dbService_, cacheManager_);
        auto filtered = filterRoleRecords(records, query.status, query.pagination.keyword, true);
        int total = static_cast<int>(filtered.size());
        auto paged = paginateRoleRecords(filtered, query.pagination.page, query.pagination.pageSize);

        std::vector<SystemHelpers::RoleListItemSummary> items;
        items.reserve(paged.size());
        std::vector<int> roleIds;
        roleIds.reserve(paged.size());
        for (const auto& record : paged) {
            SystemHelpers::RoleListItemSummary item;
            item.role = record;
            roleIds.push_back(item.role.id);
            items.push_back(std::move(item));
        }

        auto cachedMenuIds = co_await cacheManager_.getRoleMenuIdsBatch(roleIds);
        std::vector<bool> cacheHits(items.size(), false);
        std::vector<int> missingRoleIds;
        missingRoleIds.reserve(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            if (i < cachedMenuIds.size() && cachedMenuIds[i]) {
                items[i].menuIds = *cachedMenuIds[i];
                cacheHits[i] = true;
            } else {
                missingRoleIds.push_back(items[i].role.id);
            }
        }

        std::unordered_map<int, std::vector<int>> loadedMenuIds;
        if (!missingRoleIds.empty()) {
            loadedMenuIds = co_await SystemDataLoader::loadRoleMenuIdsByIds(dbService_, missingRoleIds);
        }

        for (size_t i = 0; i < items.size(); ++i) {
            if (cacheHits[i]) {
                continue;
            }

            auto it = loadedMenuIds.find(items[i].role.id);
            if (it != loadedMenuIds.end()) {
                items[i].menuIds = it->second;
            } else {
                items[i].menuIds.clear();
            }

            co_await cacheManager_.cacheRoleMenuIds(items[i].role.id, items[i].menuIds);
        }

        co_return std::make_tuple(items, total);
    }

    Task<std::vector<SystemHelpers::RoleSummary>> all() {
        auto records = co_await SystemDataLoader::loadRoleRecords(dbService_, cacheManager_);
        std::vector<SystemHelpers::RoleSummary> items;
        items.reserve(records.size());
        for (const auto& record : records) {
            if (record.status != "enabled" || record.code == SystemConstants::SUPERADMIN_ROLE_CODE) {
                continue;
            }
            SystemHelpers::RoleSummary role;
            role.id = record.id;
            role.name = record.name;
            role.code = record.code;
            items.push_back(role);
        }
        co_return items;
    }

    Task<SystemHelpers::RoleDetailSummary> detail(int id) {
        auto records = co_await SystemDataLoader::loadRoleRecords(dbService_, cacheManager_);
        auto it = std::find_if(records.begin(), records.end(), [id](const auto& record) {
            return record.id == id;
        });
        if (it == records.end()) throw NotFoundException("角色不存在");
        SystemHelpers::RoleDetailSummary item;
        item.role = *it;
        item.menuIds = co_await getRoleMenuIds(id);
        item.menus = co_await SystemRelationLoader::loadRoleMenus(dbService_, cacheManager_, id);
        co_return item;
    }

    Task<void> create(const SystemRequests::RoleCreateRequest& data) {
        auto tx = co_await TransactionGuard::create(dbService_);
        if (!data.code.empty()) {
            co_await SystemEntityGuard::ensureUniqueValue(tx, "sys_role", "code", data.code, 0, "角色编码已存在");
        }

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

        // 提交成功后使授权相关缓存失效
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateAuthorizationCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateRoleRecordsCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

    Task<void> update(int id, const SystemRequests::RoleUpdateRequest& data) {
        auto tx = co_await TransactionGuard::create(dbService_);
        auto role = co_await SystemEntityGuard::lockRoleRecordForUpdate(tx, id);
        if (SystemHelpers::isRoleCode(role, SystemConstants::SUPERADMIN_ROLE_CODE))
            throw ForbiddenException("不能编辑超级管理员角色");
        if (data.code && !data.code->empty()) {
            co_await SystemEntityGuard::ensureUniqueValue(tx, "sys_role", "code", *data.code, id, "角色编码已存在");
        }

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

        // 提交成功后使授权相关缓存失效
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateAuthorizationCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateRoleRecordsCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

    Task<void> remove(int id) {
        auto tx = co_await TransactionGuard::create(dbService_);
        auto role = co_await SystemEntityGuard::lockRoleRecordForUpdate(tx, id);
        if (SystemHelpers::isRoleCode(role, SystemConstants::SUPERADMIN_ROLE_CODE)) {
            throw ForbiddenException("不能删除超级管理员角色");
        }

        if (co_await SystemEntityGuard::hasRoleUserUsage(tx, id)) {
            throw ValidationException("该角色已被用户使用，无法删除");
        }

        co_await tx.execSqlCoro("DELETE FROM sys_role_menu WHERE roleId = ?", {std::to_string(id)});
        co_await tx.execSqlCoro("UPDATE sys_role SET deletedAt = ? WHERE id = ?", {TimestampHelper::now(), std::to_string(id)});

        // 提交成功后使授权相关缓存失效
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateAuthorizationCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateRoleRecordsCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

private:
    Task<std::vector<int>> getRoleMenuIds(int roleId) {
        auto cached = co_await cacheManager_.getRoleMenuIds(roleId);
        if (cached) {
            co_return *cached;
        }

        auto menuIds = co_await SystemDataLoader::loadRoleMenuIds(dbService_, roleId);

        co_await cacheManager_.cacheRoleMenuIds(roleId, menuIds);
        co_return menuIds;
    }

    static std::vector<SystemHelpers::RoleRecordSummary> filterRoleRecords(
        const std::vector<SystemHelpers::RoleRecordSummary>& records,
        const std::optional<std::string>& status,
        const std::string& keyword,
        bool excludeSuperadmin) {
        std::vector<SystemHelpers::RoleRecordSummary> items;
        items.reserve(records.size());

        for (const auto& record : records) {
            if (excludeSuperadmin && record.code == SystemConstants::SUPERADMIN_ROLE_CODE) {
                continue;
            }

            if (status && record.status != *status) {
                continue;
            }

            if (!keyword.empty() &&
                !SystemStringUtils::containsIgnoreCase(record.name, keyword) &&
                !SystemStringUtils::containsIgnoreCase(record.code, keyword)) {
                continue;
            }

            items.push_back(record);
        }

        return items;
    }

    static std::vector<SystemHelpers::RoleRecordSummary> paginateRoleRecords(
        const std::vector<SystemHelpers::RoleRecordSummary>& records,
        int page,
        int pageSize) {
        if (pageSize <= 0) {
            return records;
        }

        const long long safePage = std::max(page, 1);
        const long long offset = static_cast<long long>(safePage - 1) * pageSize;
        if (offset >= static_cast<long long>(records.size())) {
            return {};
        }

        const auto begin = records.begin() + static_cast<std::vector<SystemHelpers::RoleRecordSummary>::difference_type>(offset);
        const auto end = records.begin() + static_cast<std::vector<SystemHelpers::RoleRecordSummary>::difference_type>(
            std::min<long long>(offset + pageSize, static_cast<long long>(records.size())));
        return std::vector<SystemHelpers::RoleRecordSummary>(begin, end);
    }

    Task<void> syncMenus(TransactionGuard& tx, int roleId, const std::vector<int>& menuIds) {
        co_await SystemEntityGuard::ensureEnabledIds(tx, "sys_menu", menuIds, "菜单不存在或已禁用");
        co_await tx.execSqlCoro("DELETE FROM sys_role_menu WHERE roleId = ?", {std::to_string(roleId)});

        std::vector<int> uniqueMenuIds = menuIds;
        std::sort(uniqueMenuIds.begin(), uniqueMenuIds.end());
        uniqueMenuIds.erase(std::unique(uniqueMenuIds.begin(), uniqueMenuIds.end()), uniqueMenuIds.end());

        if (!uniqueMenuIds.empty()) {
            std::string sql = "INSERT INTO sys_role_menu (roleId, menuId) VALUES ";
            std::vector<std::string> valueParts, params;
            for (int menuId : uniqueMenuIds) {
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
