#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <drogon/drogon.h>

#include "common/cache/CacheManager.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/utils/FieldHelper.hpp"
#include "SystemHelpers.hpp"
#include "SystemRecordLoader.hpp"

namespace SystemDataLoader {

inline Task<std::vector<SystemHelpers::UserRecordSummary>> loadUserRecords(
    DatabaseService& dbService,
    CacheManager& cacheManager) {
    co_return co_await SystemRecordLoader::loadCachedRecords<SystemHelpers::UserRecordSummary>(
        [&cacheManager]() -> Task<std::optional<std::vector<SystemHelpers::UserRecordSummary>>> {
            co_return co_await cacheManager.getUserRecords();
        },
        [&dbService]() -> Task<std::vector<SystemHelpers::UserRecordSummary>> {
            auto result = co_await dbService.execSqlCoro(
                "SELECT u.*, d.name as departmentName FROM sys_user u LEFT JOIN sys_department d ON u.departmentId = d.id AND d.deletedAt IS NULL WHERE u.deletedAt IS NULL ORDER BY u.id ASC");

            std::vector<SystemHelpers::UserRecordSummary> records;
            records.reserve(result.size());
            for (const auto& row : result) {
                records.push_back(SystemHelpers::userRecordFromRow(row));
            }

            co_return records;
        },
        [&cacheManager](const std::vector<SystemHelpers::UserRecordSummary>& records) -> Task<void> {
            co_await cacheManager.cacheUserRecords(records);
        }
    );
}

inline Task<std::vector<SystemHelpers::RoleRecordSummary>> loadRoleRecords(
    DatabaseService& dbService,
    CacheManager& cacheManager) {
    co_return co_await SystemRecordLoader::loadCachedRecords<SystemHelpers::RoleRecordSummary>(
        [&cacheManager]() -> Task<std::optional<std::vector<SystemHelpers::RoleRecordSummary>>> {
            co_return co_await cacheManager.getRoleRecords();
        },
        [&dbService]() -> Task<std::vector<SystemHelpers::RoleRecordSummary>> {
            auto result = co_await dbService.execSqlCoro(
                "SELECT * FROM sys_role WHERE deletedAt IS NULL ORDER BY id ASC");

            std::vector<SystemHelpers::RoleRecordSummary> records;
            records.reserve(result.size());
            for (const auto& row : result) {
                records.push_back(SystemHelpers::roleRecordFromRow(row));
            }

            co_return records;
        },
        [&cacheManager](const std::vector<SystemHelpers::RoleRecordSummary>& records) -> Task<void> {
            co_await cacheManager.cacheRoleRecords(records);
        }
    );
}

inline Task<std::vector<SystemHelpers::MenuRecordSummary>> loadMenuRecords(
    DatabaseService& dbService,
    CacheManager& cacheManager) {
    co_return co_await SystemRecordLoader::loadCachedRecords<SystemHelpers::MenuRecordSummary>(
        [&cacheManager]() -> Task<std::optional<std::vector<SystemHelpers::MenuRecordSummary>>> {
            co_return co_await cacheManager.getMenuRecords();
        },
        [&dbService]() -> Task<std::vector<SystemHelpers::MenuRecordSummary>> {
            auto result = co_await dbService.execSqlCoro(
                "SELECT * FROM sys_menu WHERE deletedAt IS NULL ORDER BY `order` ASC, id ASC");

            std::vector<SystemHelpers::MenuRecordSummary> records;
            records.reserve(result.size());
            for (const auto& row : result) {
                records.push_back(SystemHelpers::menuRecordFromRow(row));
            }

            co_return records;
        },
        [&cacheManager](const std::vector<SystemHelpers::MenuRecordSummary>& records) -> Task<void> {
            co_await cacheManager.cacheMenuRecords(records);
        }
    );
}

inline Task<std::vector<SystemHelpers::DepartmentRecordSummary>> loadDepartmentRecords(
    DatabaseService& dbService,
    CacheManager& cacheManager) {
    co_return co_await SystemRecordLoader::loadCachedRecords<SystemHelpers::DepartmentRecordSummary>(
        [&cacheManager]() -> Task<std::optional<std::vector<SystemHelpers::DepartmentRecordSummary>>> {
            co_return co_await cacheManager.getDepartmentRecords();
        },
        [&dbService]() -> Task<std::vector<SystemHelpers::DepartmentRecordSummary>> {
            auto result = co_await dbService.execSqlCoro(
                "SELECT * FROM sys_department WHERE deletedAt IS NULL ORDER BY `order` ASC, id ASC");

            std::vector<SystemHelpers::DepartmentRecordSummary> records;
            records.reserve(result.size());
            for (const auto& row : result) {
                records.push_back(SystemHelpers::departmentRecordFromRow(row));
            }

            co_return records;
        },
        [&cacheManager](const std::vector<SystemHelpers::DepartmentRecordSummary>& records) -> Task<void> {
            co_await cacheManager.cacheDepartmentRecords(records);
        }
    );
}

inline Task<std::vector<int>> loadUserRoleIds(DatabaseService& dbService, int userId) {
    auto result = co_await dbService.execSqlCoro(
        "SELECT roleId FROM sys_user_role WHERE userId = ?",
        {std::to_string(userId)});

    std::vector<int> roleIds;
    roleIds.reserve(result.size());
    for (const auto& row : result) {
        roleIds.push_back(F_INT(row["roleId"]));
    }

    co_return roleIds;
}

inline Task<std::unordered_map<int, std::vector<int>>> loadUserRoleIdsByIds(
    DatabaseService& dbService,
    const std::vector<int>& userIds) {
    std::unordered_map<int, std::vector<int>> roleIdsByUser;
    if (userIds.empty()) {
        co_return roleIdsByUser;
    }

    std::string sql = R"(
        SELECT ur.userId, ur.roleId
        FROM sys_user_role ur
        WHERE ur.userId IN (
    )";
    std::vector<std::string> params;
    params.reserve(userIds.size());
    for (size_t i = 0; i < userIds.size(); ++i) {
        if (i > 0) {
            sql += ", ";
        }
        sql += "?";
        params.push_back(std::to_string(userIds[i]));
    }
    sql += R"(
        )
        ORDER BY ur.userId ASC, ur.roleId ASC
    )";

    auto result = co_await dbService.execSqlCoro(sql, params);
    for (const auto& row : result) {
        roleIdsByUser[F_INT(row["userId"])].push_back(F_INT(row["roleId"]));
    }

    co_return roleIdsByUser;
}

inline Task<std::vector<int>> loadRoleMenuIds(DatabaseService& dbService, int roleId) {
    auto result = co_await dbService.execSqlCoro(
        "SELECT menuId FROM sys_role_menu WHERE roleId = ?",
        {std::to_string(roleId)});

    std::vector<int> menuIds;
    menuIds.reserve(result.size());
    for (const auto& row : result) {
        menuIds.push_back(F_INT(row["menuId"]));
    }

    co_return menuIds;
}

inline Task<std::unordered_map<int, std::vector<int>>> loadRoleMenuIdsByIds(
    DatabaseService& dbService,
    const std::vector<int>& roleIds) {
    std::unordered_map<int, std::vector<int>> menuIdsByRole;
    if (roleIds.empty()) {
        co_return menuIdsByRole;
    }

    std::string sql = R"(
        SELECT roleId, menuId
        FROM sys_role_menu
        WHERE roleId IN (
    )";
    std::vector<std::string> params;
    params.reserve(roleIds.size());
    for (size_t i = 0; i < roleIds.size(); ++i) {
        if (i > 0) {
            sql += ", ";
        }
        sql += "?";
        params.push_back(std::to_string(roleIds[i]));
    }
    sql += R"(
        )
        ORDER BY roleId ASC, menuId ASC
    )";

    auto result = co_await dbService.execSqlCoro(sql, params);
    for (const auto& row : result) {
        menuIdsByRole[F_INT(row["roleId"])].push_back(F_INT(row["menuId"]));
    }

    co_return menuIdsByRole;
}

}  // namespace SystemDataLoader
