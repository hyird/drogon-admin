#pragma once

#include <exception>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <drogon/drogon.h>

#include "common/cache/CacheManager.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/database/RedisService.hpp"
#include "modules/home/HomeHelpers.hpp"
#include "modules/system/SystemHelpers.hpp"
#include "modules/system/SystemConstants.hpp"

namespace StartupTasks {

inline bool checkRedisAvailable() {
    if (!AppRedisConfig::enabled()) {
        LOG_INFO << "Redis cache is disabled by configuration";
        return true;
    }

    const bool preferFast = AppRedisConfig::useFast();
    auto tryFast = []() -> bool {
        try {
            auto client = app().getFastRedisClient("default");
            return static_cast<bool>(client);
        } catch (const std::exception&) {
            return false;
        }
    };
    auto tryNormal = []() -> bool {
        try {
            auto client = app().getRedisClient("default");
            return static_cast<bool>(client);
        } catch (const std::exception& e) {
            LOG_WARN << "Redis is not available: " << e.what();
            return false;
        }
    };

    if (preferFast) {
        if (tryFast()) {
            AppRedisConfig::useFast() = true;
            LOG_INFO << "Redis is available (fast mode: true)";
            return true;
        }

        if (tryNormal()) {
            AppRedisConfig::useFast() = false;
            LOG_INFO << "Redis is available (fast mode: false)";
            return true;
        }
    } else {
        if (tryNormal()) {
            AppRedisConfig::useFast() = false;
            LOG_INFO << "Redis is available (fast mode: false)";
            return true;
        }

        if (tryFast()) {
            AppRedisConfig::useFast() = true;
            LOG_INFO << "Redis is available (fast mode: true)";
            return true;
        }
    }

    return false;
}

inline drogon::Task<> warmupCache() {
    if (!AppRedisConfig::enabled()) {
        LOG_INFO << "Cache is disabled, skipping warmup";
        co_return;
    }

    try {
        DatabaseService dbService;
        CacheManager cacheManager;

        std::vector<SystemHelpers::UserRecordSummary> userRecords;
        std::vector<SystemHelpers::RoleRecordSummary> roleRecords;
        std::vector<SystemHelpers::MenuRecordSummary> menuRecords;
        std::vector<SystemHelpers::DepartmentRecordSummary> departmentRecords;

        {
            auto result = co_await dbService.execSqlCoro(
                "SELECT u.*, d.name as departmentName FROM sys_user u "
                "LEFT JOIN sys_department d ON u.departmentId = d.id AND d.deletedAt IS NULL "
                "WHERE u.deletedAt IS NULL ORDER BY u.id ASC");
            userRecords.reserve(result.size());
            for (const auto& row : result) {
                userRecords.push_back(SystemHelpers::userRecordFromRow(row));
            }
            co_await cacheManager.cacheUserRecords(userRecords);
        }

        {
            auto result = co_await dbService.execSqlCoro(
                "SELECT * FROM sys_role WHERE deletedAt IS NULL ORDER BY id ASC");
            roleRecords.reserve(result.size());
            for (const auto& row : result) {
                roleRecords.push_back(SystemHelpers::roleRecordFromRow(row));
            }
            co_await cacheManager.cacheRoleRecords(roleRecords);
        }

        {
            auto result = co_await dbService.execSqlCoro(
                "SELECT id, name, parentId, type, path, component, permissionCode, "
                "icon, status, `order`, 1 as visible "
                "FROM sys_menu "
                "WHERE status = 'enabled' AND deletedAt IS NULL "
                "ORDER BY `order` ASC, id ASC");
            std::vector<SystemHelpers::MenuSummary> menus;
            menus.reserve(result.size());
            menuRecords.reserve(result.size());
            for (const auto& row : result) {
                auto record = SystemHelpers::menuRecordFromRow(row);
                menuRecords.push_back(record);
                menus.push_back(SystemHelpers::menuSummaryFromMenuRecord(record));
            }
            co_await cacheManager.cacheMenuRecords(menuRecords);
            co_await cacheManager.cacheAllMenus(menus);
        }

        {
            auto result = co_await dbService.execSqlCoro(
                "SELECT * FROM sys_department WHERE deletedAt IS NULL ORDER BY `order` ASC, id ASC");
            departmentRecords.reserve(result.size());
            for (const auto& row : result) {
                departmentRecords.push_back(SystemHelpers::departmentRecordFromRow(row));
            }
            co_await cacheManager.cacheDepartmentRecords(departmentRecords);
        }

        std::vector<SystemHelpers::RoleSummary> enabledRoles;
        enabledRoles.reserve(roleRecords.size());
        for (const auto& role : roleRecords) {
            if (role.status != "enabled" || role.code == SystemConstants::SUPERADMIN_ROLE_CODE) {
                continue;
            }
            enabledRoles.push_back(SystemHelpers::RoleSummary{role.id, role.name, role.code});
        }
        if (!enabledRoles.empty()) {
            co_await cacheManager.cacheAllRoles(enabledRoles);
        }

        std::vector<SystemHelpers::MenuSummary> enabledMenus;
        enabledMenus.reserve(menuRecords.size());
        std::unordered_map<int, SystemHelpers::RoleSummary> enabledRoleSummaryMap;
        enabledRoleSummaryMap.reserve(roleRecords.size());
        std::unordered_map<int, std::vector<int>> roleMenuIdsByRole;
        std::unordered_map<int, std::vector<int>> userRoleIdsByUser;

        for (const auto& role : roleRecords) {
            if (role.status != "enabled") {
                continue;
            }

            enabledRoleSummaryMap.emplace(role.id, SystemHelpers::RoleSummary{role.id, role.name, role.code});
        }

        for (const auto& record : menuRecords) {
            if (record.status != "enabled") {
                continue;
            }
            enabledMenus.push_back(SystemHelpers::menuSummaryFromMenuRecord(record));
        }

        if (!roleRecords.empty()) {
            auto result = co_await dbService.execSqlCoro(
                "SELECT rm.roleId, rm.menuId FROM sys_role_menu rm "
                "INNER JOIN sys_role r ON rm.roleId = r.id AND r.deletedAt IS NULL "
                "ORDER BY rm.roleId ASC, rm.menuId ASC");
            for (const auto& row : result) {
                roleMenuIdsByRole[F_INT(row["roleId"])].push_back(F_INT(row["menuId"]));
            }
        }

        if (!userRecords.empty()) {
            auto result = co_await dbService.execSqlCoro(
                "SELECT ur.userId, ur.roleId FROM sys_user_role ur "
                "INNER JOIN sys_user u ON ur.userId = u.id AND u.deletedAt IS NULL "
                "ORDER BY ur.userId ASC, ur.roleId ASC");
            for (const auto& row : result) {
                userRoleIdsByUser[F_INT(row["userId"])].push_back(F_INT(row["roleId"]));
            }
        }

        for (const auto& user : userRecords) {
            const auto roleIt = userRoleIdsByUser.find(user.id);
            const std::vector<int> roleIds = roleIt != userRoleIdsByUser.end()
                ? roleIt->second
                : std::vector<int>{};

            co_await cacheManager.cacheUserRoleIds(user.id, roleIds);

            std::vector<SystemHelpers::RoleSummary> userRoles;
            userRoles.reserve(roleIds.size());
            std::vector<int> enabledRoleIds;
            enabledRoleIds.reserve(roleIds.size());
            bool isSuperadmin = false;
            for (int roleId : roleIds) {
                auto summaryIt = enabledRoleSummaryMap.find(roleId);
                if (summaryIt == enabledRoleSummaryMap.end()) {
                    continue;
                }

                userRoles.push_back(summaryIt->second);
                enabledRoleIds.push_back(roleId);
                if (summaryIt->second.code == SystemConstants::SUPERADMIN_ROLE_CODE) {
                    isSuperadmin = true;
                }
            }
            co_await cacheManager.cacheUserRoles(user.id, userRoles);

            std::vector<SystemHelpers::MenuSummary> userMenus;
            if (isSuperadmin) {
                userMenus = enabledMenus;
            } else if (!enabledRoleIds.empty()) {
                std::unordered_set<int> menuIds;
                for (int roleId : enabledRoleIds) {
                    auto menuIt = roleMenuIdsByRole.find(roleId);
                    if (menuIt == roleMenuIdsByRole.end()) {
                        continue;
                    }
                    menuIds.insert(menuIt->second.begin(), menuIt->second.end());
                }

                userMenus.reserve(menuIds.size());
                for (const auto& record : menuRecords) {
                    if (record.status != "enabled") {
                        continue;
                    }
                    if (menuIds.find(record.id) == menuIds.end()) {
                        continue;
                    }
                    userMenus.push_back(SystemHelpers::menuSummaryFromMenuRecord(record));
                }
            }

            co_await cacheManager.cacheUserMenus(user.id, userMenus);
        }

        for (const auto& role : roleRecords) {
            const auto menuIt = roleMenuIdsByRole.find(role.id);
            if (menuIt != roleMenuIdsByRole.end()) {
                co_await cacheManager.cacheRoleMenuIds(role.id, menuIt->second);
            } else {
                co_await cacheManager.cacheRoleMenuIds(role.id, std::vector<int>{});
            }
        }

        if (!userRecords.empty() || !roleRecords.empty() || !menuRecords.empty() || !departmentRecords.empty()) {
            HomeHelpers::HomeStatsSummary stats;
            stats.userCount = static_cast<int>(userRecords.size());
            stats.roleCount = static_cast<int>(roleRecords.size());
            stats.menuCount = static_cast<int>(menuRecords.size());
            stats.departmentCount = static_cast<int>(departmentRecords.size());
            co_await cacheManager.cacheHomeStats(stats);
        }

        LOG_INFO << "Cache warmup completed: "
                 << userRecords.size() << " users, "
                 << roleRecords.size() << " roles, "
                 << menuRecords.size() << " menus, "
                 << departmentRecords.size() << " departments loaded, "
                 << userRoleIdsByUser.size() << " user role groups and "
                 << roleMenuIdsByRole.size() << " role menu groups warmed";
    } catch (const std::exception& e) {
        LOG_ERROR << "Cache warmup failed: " << e.what();
    }
}

}  // namespace StartupTasks
