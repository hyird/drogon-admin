#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <drogon/drogon.h>

#include "common/cache/CacheManager.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/utils/FieldHelper.hpp"
#include "modules/system/SystemConstants.hpp"
#include "modules/system/SystemHelpers.hpp"

namespace PermissionResolver {

inline Task<bool> isUserEnabled(int userId) {
    CacheManager cacheManager;
    auto session = co_await cacheManager.getUserSession(userId);
    if (session) {
        co_return session->status != "disabled";
    }

    auto cachedRecords = co_await cacheManager.getUserRecords();
    if (cachedRecords) {
        for (const auto& record : *cachedRecords) {
            if (record.id == userId) {
                co_return record.status != "disabled";
            }
        }
        co_return false;
    }

    DatabaseService dbService;
    auto dbClient = dbService.getClient();
    if (!dbClient) {
        co_return false;
    }

    auto result = co_await dbClient->execSqlCoro(buildSql(
        R"(
            SELECT status
            FROM sys_user
            WHERE id = ? AND deletedAt IS NULL
            LIMIT 1
        )",
        {std::to_string(userId)}));
    if (result.empty()) {
        co_return false;
    }

    co_return result[0]["status"].as<std::string>() != "disabled";
}

inline Task<std::optional<std::unordered_map<int, SystemHelpers::RoleSummary>>> loadEnabledRoleMap(
    CacheManager& cacheManager) {
    auto roleRecords = co_await cacheManager.getRoleRecords();
    if (!roleRecords) {
        co_return std::nullopt;
    }

    co_return SystemHelpers::enabledRoleSummaryMapFromRecords(*roleRecords);
}

inline bool hasAnyRequiredPermission(const std::unordered_set<std::string>& permissionCodes,
                                     const std::vector<std::string>& requiredPermissions) {
    for (const auto& requiredPermission : requiredPermissions) {
        if (permissionCodes.find(requiredPermission) != permissionCodes.end()) {
            return true;
        }
    }

    return false;
}

inline Task<std::optional<bool>> tryHasPermissionFromSession(
    int userId,
    const std::vector<std::string>& requiredPermissions) {
    CacheManager cacheManager;
    auto session = co_await cacheManager.getUserSession(userId);
    auto enabledRoleMap = co_await loadEnabledRoleMap(cacheManager);
    if (!enabledRoleMap) {
        co_return std::nullopt;
    }

    if (!session) {
        auto cachedRoles = co_await cacheManager.getUserRoles(userId);
        std::vector<int> roleIds;
        if (cachedRoles) {
            roleIds.reserve(cachedRoles->size());
            for (const auto& role : *cachedRoles) {
                auto it = enabledRoleMap->find(role.id);
                if (it == enabledRoleMap->end()) {
                    continue;
                }

                roleIds.push_back(it->second.id);
                if (SystemHelpers::isRoleCode(it->second, SystemConstants::SUPERADMIN_ROLE_CODE)) {
                    co_return true;
                }
            }
        } else {
            auto cachedRoleIds = co_await cacheManager.getUserRoleIds(userId);
            if (!cachedRoleIds) {
                co_return std::nullopt;
            }

            roleIds.reserve(cachedRoleIds->size());
            for (int roleId : *cachedRoleIds) {
                auto it = enabledRoleMap->find(roleId);
                if (it == enabledRoleMap->end()) {
                    continue;
                }

                roleIds.push_back(roleId);
                if (SystemHelpers::isRoleCode(it->second, SystemConstants::SUPERADMIN_ROLE_CODE)) {
                    co_return true;
                }
            }
        }

        if (roleIds.empty()) {
            co_return false;
        }

        auto cachedRoleMenus = co_await cacheManager.getRoleMenuIdsBatch(roleIds);
        std::unordered_set<int> menuIds;
        for (const auto& maybeMenuIds : cachedRoleMenus) {
            if (!maybeMenuIds) {
                co_return std::nullopt;
            }

            menuIds.insert(maybeMenuIds->begin(), maybeMenuIds->end());
        }

        auto menuRecords = co_await cacheManager.getMenuRecords();
        if (!menuRecords) {
            co_return std::nullopt;
        }

        auto permissionCodes = SystemHelpers::permissionCodeSetFromMenuRecords(*menuRecords, menuIds);
        co_return hasAnyRequiredPermission(permissionCodes, requiredPermissions);
    }

    std::vector<SystemHelpers::RoleSummary> enabledSessionRoles;
    enabledSessionRoles.reserve(session->roles.size());
    for (const auto& role : session->roles) {
        auto it = enabledRoleMap->find(role.id);
        if (it == enabledRoleMap->end()) {
            co_return std::nullopt;
        }

        enabledSessionRoles.push_back(it->second);
    }

    if (SystemHelpers::hasSuperadminRole(enabledSessionRoles)) {
        co_return true;
    }

    std::unordered_set<std::string> permissionCodes;
    const std::unordered_set<std::string>* permissionCodeSource = &session->permissionCodeSet;
    if (permissionCodeSource->empty()) {
        permissionCodes = session->permissionCodes.empty()
            ? SystemHelpers::permissionCodeSetFromMenus(session->menus)
            : SystemHelpers::permissionCodeSetFromCodes(session->permissionCodes);
        permissionCodeSource = &permissionCodes;
    }

    co_return hasAnyRequiredPermission(*permissionCodeSource, requiredPermissions);
}

inline Task<bool> hasPermissionViaDatabase(int userId, const std::vector<std::string>& requiredPermissions) {
    DatabaseService dbService;
    auto dbClient = dbService.getClient();
    if (!dbClient) {
        LOG_ERROR << "Database client is not available for permission check";
        co_return false;
    }

    auto superadminResult = co_await dbClient->execSqlCoro(buildSql(R"(
        SELECT 1 as hit
        FROM sys_user_role ur
        INNER JOIN sys_role r ON ur.roleId = r.id
        WHERE ur.userId = ? AND r.code = ? AND r.status = 'enabled' AND r.deletedAt IS NULL
        LIMIT 1
    )", {std::to_string(userId), std::string(SystemConstants::SUPERADMIN_ROLE_CODE)}));

    if (!superadminResult.empty()) {
        co_return true;
    }

    std::vector<std::string> placeholders;
    for (size_t i = 0; i < requiredPermissions.size(); ++i) {
        placeholders.push_back("?");
    }
    std::string permissionsPlaceholder;
    for (size_t i = 0; i < placeholders.size(); ++i) {
        if (i > 0) permissionsPlaceholder += ", ";
        permissionsPlaceholder += placeholders[i];
    }

    std::string checkPermissionSqlTemplate = R"(
        SELECT 1 as hit
        FROM sys_menu m
        INNER JOIN sys_role_menu rm ON m.id = rm.menuId
        INNER JOIN sys_user_role ur ON rm.roleId = ur.roleId
        INNER JOIN sys_role r ON ur.roleId = r.id
        WHERE ur.userId = ?
          AND r.status = 'enabled'
          AND r.deletedAt IS NULL
          AND m.permissionCode IN ()" + permissionsPlaceholder + R"()
          AND m.deletedAt IS NULL
        LIMIT 1
    )";

    std::vector<std::string> params;
    params.push_back(std::to_string(userId));
    for (const auto& perm : requiredPermissions) {
        params.push_back(perm);
    }

    auto permResult = co_await dbClient->execSqlCoro(buildSql(checkPermissionSqlTemplate, params));
    co_return !permResult.empty();
}

}  // namespace PermissionResolver
