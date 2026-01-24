#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/orm/DbClient.h>
#include "common/utils/Response.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/database/DatabaseService.hpp"

using namespace drogon;
using namespace drogon::orm;

/**
 * @brief 权限检查工具类
 */
class PermissionChecker {
public:
    /**
     * @brief 检查用户是否有指定权限
     */
    static Task<bool> hasPermission(int userId, const std::vector<std::string>& requiredPermissions) {
        if (requiredPermissions.empty()) {
            co_return true;
        }

        auto dbClient = AppDbConfig::useFast()
            ? app().getFastDbClient("default")
            : app().getDbClient("default");

        // 检查是否是超级管理员
        auto superadminResult = co_await dbClient->execSqlCoro(buildSql(R"(
            SELECT COUNT(*) as count
            FROM sys_user_role ur
            INNER JOIN sys_role r ON ur.roleId = r.id
            WHERE ur.userId = ? AND r.code = 'superadmin' AND r.deletedAt IS NULL
        )", {std::to_string(userId)}));

        if (!superadminResult.empty() && F_INT(superadminResult[0]["count"]) > 0) {
            co_return true;
        }

        // 检查权限
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
            SELECT COUNT(DISTINCT m.permissionCode) as count
            FROM sys_menu m
            INNER JOIN sys_role_menu rm ON m.id = rm.menuId
            INNER JOIN sys_user_role ur ON rm.roleId = ur.roleId
            INNER JOIN sys_role r ON ur.roleId = r.id
            WHERE ur.userId = ?
              AND r.status = 'enabled'
              AND r.deletedAt IS NULL
              AND m.permissionCode IN ()" + permissionsPlaceholder + R"()
              AND m.deletedAt IS NULL
        )";

        std::vector<std::string> params;
        params.push_back(std::to_string(userId));
        for (const auto& perm : requiredPermissions) {
            params.push_back(perm);
        }

        auto permResult = co_await dbClient->execSqlCoro(buildSql(checkPermissionSqlTemplate, params));

        co_return !permResult.empty() && F_INT(permResult[0]["count"]) > 0;
    }

    /**
     * @brief 检查权限，无权限时抛出异常
     */
    static Task<void> checkPermission(int userId, const std::vector<std::string>& requiredPermissions) {
        bool hasPermit = co_await hasPermission(userId, requiredPermissions);
        if (!hasPermit) {
            throw ForbiddenException("无权限访问此资源");
        }
    }
};
