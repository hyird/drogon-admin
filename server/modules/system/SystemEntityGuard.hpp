#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "common/database/TransactionGuard.hpp"
#include "common/utils/AppException.hpp"
#include "SystemHelpers.hpp"

namespace SystemEntityGuard {

inline std::vector<int> uniqueSortedIds(const std::vector<int>& ids) {
    std::vector<int> uniqueIds = ids;
    std::sort(uniqueIds.begin(), uniqueIds.end());
    uniqueIds.erase(std::unique(uniqueIds.begin(), uniqueIds.end()), uniqueIds.end());
    return uniqueIds;
}

inline drogon::Task<void> lockRowForUpdate(TransactionGuard& tx,
                                           std::string_view table,
                                           int id,
                                           std::string_view notFoundMessage) {
    std::string sql = "SELECT id FROM ";
    sql += table;
    sql += " WHERE id = ? AND deletedAt IS NULL FOR UPDATE";

    auto result = co_await tx.execSqlCoro(sql, {std::to_string(id)});
    if (result.empty()) {
        throw NotFoundException(std::string(notFoundMessage));
    }
}

inline drogon::Task<void> lockEnabledRowForUpdate(TransactionGuard& tx,
                                                  std::string_view table,
                                                  int id,
                                                  std::string_view notFoundMessage) {
    std::string sql = "SELECT id FROM ";
    sql += table;
    sql += " WHERE id = ? AND status = 'enabled' AND deletedAt IS NULL FOR UPDATE";

    auto result = co_await tx.execSqlCoro(sql, {std::to_string(id)});
    if (result.empty()) {
        throw NotFoundException(std::string(notFoundMessage));
    }
}

inline drogon::Task<SystemHelpers::UserRecordSummary> lockUserRecordForUpdate(TransactionGuard& tx,
                                                                            int userId) {
    auto result = co_await tx.execSqlCoro(
        R"(
            SELECT u.*, d.name as departmentName
            FROM sys_user u
            LEFT JOIN sys_department d ON u.departmentId = d.id AND d.deletedAt IS NULL
            WHERE u.id = ? AND u.deletedAt IS NULL
            FOR UPDATE
        )",
        {std::to_string(userId)});
    if (result.empty()) {
        throw NotFoundException("用户不存在");
    }

    co_return SystemHelpers::userRecordFromRow(result[0]);
}

inline drogon::Task<SystemHelpers::RoleRecordSummary> lockRoleRecordForUpdate(TransactionGuard& tx,
                                                                            int roleId) {
    auto result = co_await tx.execSqlCoro(
        "SELECT * FROM sys_role WHERE id = ? AND deletedAt IS NULL FOR UPDATE",
        {std::to_string(roleId)});
    if (result.empty()) {
        throw NotFoundException("角色不存在");
    }

    co_return SystemHelpers::roleRecordFromRow(result[0]);
}

inline drogon::Task<bool> existsRow(TransactionGuard& tx,
                                    std::string_view sql,
                                    const std::vector<std::string>& params) {
    auto result = co_await tx.execSqlCoro(std::string(sql), params);
    co_return !result.empty();
}

inline drogon::Task<bool> hasRoleUserUsage(TransactionGuard& tx, int roleId) {
    co_return co_await existsRow(
        tx,
        "SELECT id FROM sys_user_role WHERE roleId = ? LIMIT 1",
        {std::to_string(roleId)});
}

inline drogon::Task<bool> hasChildDepartment(TransactionGuard& tx, int departmentId) {
    co_return co_await existsRow(
        tx,
        "SELECT id FROM sys_department WHERE parentId = ? AND deletedAt IS NULL LIMIT 1",
        {std::to_string(departmentId)});
}

inline drogon::Task<bool> hasAssignedDepartmentUsers(TransactionGuard& tx, int departmentId) {
    co_return co_await existsRow(
        tx,
        "SELECT id FROM sys_user WHERE departmentId = ? AND deletedAt IS NULL LIMIT 1",
        {std::to_string(departmentId)});
}

inline drogon::Task<bool> hasChildMenu(TransactionGuard& tx, int menuId) {
    co_return co_await existsRow(
        tx,
        "SELECT id FROM sys_menu WHERE parentId = ? AND deletedAt IS NULL LIMIT 1",
        {std::to_string(menuId)});
}

inline drogon::Task<void> ensureUniqueValue(TransactionGuard& tx,
                                           std::string_view table,
                                           std::string_view column,
                                           const std::string& value,
                                           int excludeId,
                                           std::string_view duplicateMessage) {
    std::string sql = "SELECT id FROM ";
    sql += table;
    sql += " WHERE ";
    sql += column;
    sql += " = ? AND deletedAt IS NULL";

    std::vector<std::string> params = {value};
    if (excludeId > 0) {
        sql += " AND id != ?";
        params.push_back(std::to_string(excludeId));
    }
    sql += " FOR UPDATE";

    auto result = co_await tx.execSqlCoro(sql, params);
    if (!result.empty()) {
        throw ValidationException(std::string(duplicateMessage));
    }
}

inline drogon::Task<void> ensureEnabledIds(TransactionGuard& tx,
                                           std::string_view table,
                                           const std::vector<int>& ids,
                                           std::string_view notFoundMessage) {
    if (ids.empty()) {
        co_return;
    }

    auto uniqueIds = uniqueSortedIds(ids);
    std::string sql = "SELECT id FROM ";
    sql += table;
    sql += " WHERE id IN (";

    std::vector<std::string> params;
    params.reserve(uniqueIds.size());
    for (size_t i = 0; i < uniqueIds.size(); ++i) {
        if (i > 0) {
            sql += ", ";
        }
        sql += "?";
        params.push_back(std::to_string(uniqueIds[i]));
    }
    sql += ") AND status = 'enabled' AND deletedAt IS NULL FOR UPDATE";

    auto result = co_await tx.execSqlCoro(sql, params);
    if (result.size() != uniqueIds.size()) {
        throw NotFoundException(std::string(notFoundMessage));
    }
}

}  // namespace SystemEntityGuard
