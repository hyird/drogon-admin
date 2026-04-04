#pragma once

#include <vector>

#include <drogon/HttpAppFramework.h>
#include "common/utils/Response.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/PermissionResolver.hpp"

using namespace drogon;

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

        if (!co_await PermissionResolver::isUserEnabled(userId)) {
            co_return false;
        }

        if (auto cached = co_await PermissionResolver::tryHasPermissionFromSession(userId, requiredPermissions); cached.has_value()) {
            co_return *cached;
        }

        co_return co_await PermissionResolver::hasPermissionViaDatabase(userId, requiredPermissions);
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
