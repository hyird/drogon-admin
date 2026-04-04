#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <drogon/drogon.h>

#include "common/cache/CacheManager.hpp"
#include "common/database/DatabaseService.hpp"
#include "SystemDataLoader.hpp"
#include "SystemHelpers.hpp"

namespace SystemRelationLoader {

inline Task<std::vector<SystemHelpers::RoleSummary>> loadUserRoles(
    DatabaseService& dbService,
    CacheManager& cacheManager,
    int userId) {
    auto cached = co_await cacheManager.getUserRoles(userId);
    if (cached) {
        LOG_DEBUG << "User roles cache hit for userId: " << userId;
        co_return *cached;
    }

    LOG_DEBUG << "User roles cache miss for userId: " << userId;

    auto roleIds = co_await cacheManager.getUserRoleIds(userId);
    if (!roleIds) {
        auto loadedRoleIds = co_await SystemDataLoader::loadUserRoleIds(dbService, userId);
        co_await cacheManager.cacheUserRoleIds(userId, loadedRoleIds);
        roleIds = std::move(loadedRoleIds);
    }

    auto roleRecords = co_await SystemDataLoader::loadRoleRecords(dbService, cacheManager);
    auto roleMap = SystemHelpers::enabledRoleSummaryMapFromRecords(roleRecords);

    std::vector<SystemHelpers::RoleSummary> roles;
    roles.reserve(roleIds->size());
    for (int roleId : *roleIds) {
        auto it = roleMap.find(roleId);
        if (it != roleMap.end()) {
            roles.push_back(it->second);
        }
    }

    co_await cacheManager.cacheUserRoles(userId, roles);
    co_return roles;
}

inline Task<std::unordered_map<int, std::vector<SystemHelpers::RoleSummary>>> loadUserRolesByIds(
    DatabaseService& dbService,
    CacheManager& cacheManager,
    const std::vector<int>& userIds) {
    std::unordered_map<int, std::vector<SystemHelpers::RoleSummary>> rolesByUser;
    if (userIds.empty()) {
        co_return rolesByUser;
    }

    auto roleRecords = co_await SystemDataLoader::loadRoleRecords(dbService, cacheManager);
    auto roleMap = SystemHelpers::enabledRoleSummaryMapFromRecords(roleRecords);
    auto roleIdsByUser = co_await SystemDataLoader::loadUserRoleIdsByIds(dbService, userIds);

    for (const auto& [userId, roleIds] : roleIdsByUser) {
        auto& roles = rolesByUser[userId];
        roles.reserve(roleIds.size());
        for (int roleId : roleIds) {
            auto it = roleMap.find(roleId);
            if (it != roleMap.end()) {
                roles.push_back(it->second);
            }
        }
    }

    co_return rolesByUser;
}

inline Task<std::vector<SystemHelpers::MenuSummary>> loadAllMenus(
    DatabaseService& dbService,
    CacheManager& cacheManager) {
    auto cached = co_await cacheManager.getAllMenus();
    if (cached) {
        LOG_DEBUG << "All menus cache hit";
        co_return *cached;
    }

    LOG_DEBUG << "All menus cache miss";

    auto records = co_await SystemDataLoader::loadMenuRecords(dbService, cacheManager);
    auto menus = SystemHelpers::enabledMenuSummariesFromRecords(records);
    co_await cacheManager.cacheAllMenus(menus);

    co_return menus;
}

inline Task<std::vector<SystemHelpers::MenuSummary>> loadUserMenus(
    DatabaseService& dbService,
    CacheManager& cacheManager,
    int userId,
    const std::vector<SystemHelpers::RoleSummary>& roles) {
    auto cached = co_await cacheManager.getUserMenus(userId);
    if (cached) {
        LOG_DEBUG << "User menus cache hit for userId: " << userId;
        co_return *cached;
    }

    LOG_DEBUG << "User menus cache miss for userId: " << userId;

    if (SystemHelpers::hasSuperadminRole(roles)) {
        co_return co_await loadAllMenus(dbService, cacheManager);
    }

    std::vector<int> roleIds;
    roleIds.reserve(roles.size());
    for (const auto& role : roles) {
        roleIds.push_back(role.id);
    }

    auto cachedRoleMenus = co_await cacheManager.getRoleMenuIdsBatch(roleIds);
    std::unordered_set<int> menuIdSet;
    std::vector<int> missingRoleIds;
    missingRoleIds.reserve(roleIds.size());
    for (size_t i = 0; i < roleIds.size(); ++i) {
        if (i < cachedRoleMenus.size() && cachedRoleMenus[i]) {
            menuIdSet.insert(cachedRoleMenus[i]->begin(), cachedRoleMenus[i]->end());
        } else {
            missingRoleIds.push_back(roleIds[i]);
        }
    }

    if (!missingRoleIds.empty()) {
        auto loadedRoleMenus = co_await SystemDataLoader::loadRoleMenuIdsByIds(dbService, missingRoleIds);
        for (const auto& [roleId, menuIds] : loadedRoleMenus) {
            menuIdSet.insert(menuIds.begin(), menuIds.end());
            co_await cacheManager.cacheRoleMenuIds(roleId, menuIds);
        }
    }

    auto records = co_await SystemDataLoader::loadMenuRecords(dbService, cacheManager);
    auto enabledMenus = SystemHelpers::enabledMenuSummariesFromRecords(records);
    std::vector<SystemHelpers::MenuSummary> menus;
    menus.reserve(enabledMenus.size());
    for (const auto& menu : enabledMenus) {
        if (menuIdSet.find(menu.id) == menuIdSet.end()) {
            continue;
        }
        menus.push_back(menu);
    }

    co_await cacheManager.cacheUserMenus(userId, menus);
    co_return menus;
}

inline Task<std::vector<SystemHelpers::RoleMenuSummary>> loadRoleMenus(
    DatabaseService& dbService,
    CacheManager& cacheManager,
    int roleId) {
    auto menuIds = co_await cacheManager.getRoleMenuIds(roleId);
    if (!menuIds) {
        auto loadedMenuIds = co_await SystemDataLoader::loadRoleMenuIds(dbService, roleId);
        co_await cacheManager.cacheRoleMenuIds(roleId, loadedMenuIds);
        menuIds = std::move(loadedMenuIds);
    }

    if (menuIds->empty()) {
        co_return {};
    }

    auto menuRecords = co_await SystemDataLoader::loadMenuRecords(dbService, cacheManager);
    std::vector<SystemHelpers::RoleMenuSummary> menus;
    menus.reserve(menuRecords.size());

    std::unordered_set<int> menuIdSet(menuIds->begin(), menuIds->end());
    for (const auto& record : menuRecords) {
        if (record.status != "enabled") {
            continue;
        }
        if (menuIdSet.find(record.id) == menuIdSet.end()) {
            continue;
        }
        menus.push_back(SystemHelpers::roleMenuSummaryFromMenuRecord(record));
    }

    co_return menus;
}

}  // namespace SystemRelationLoader
