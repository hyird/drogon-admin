#pragma once

#include <charconv>
#include <json/json.h>

#include "common/database/RedisService.hpp"
#include "modules/system/SystemHelpers.hpp"
#include <sstream>
#include <string>
#include <optional>

/**
 * @brief 缓存管理器
 * 提供业务层缓存操作，统一管理缓存键命名和 TTL
 */
class CacheManager {
private:
    RedisService redis_;
    int userSessionTtl_;
    int userMenusTtl_;
    int userRolesTtl_;

    static std::string serializeJson(const Json::Value& value) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, value);
    }

    static std::optional<Json::Value> parseJson(const std::string& key,
                                                const std::string& content) {
        try {
            Json::Value json;
            Json::CharReaderBuilder builder;
            std::istringstream stream(content);
            std::string errs;

            if (Json::parseFromStream(builder, stream, &json, &errs)) {
                return json;
            }

            LOG_WARN << "Failed to parse JSON from Redis key '" << key << "': " << errs;
            return std::nullopt;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis JSON parse error for key '" << key << "': " << e.what();
            return std::nullopt;
        }
    }

    static std::optional<int64_t> parseInt64Value(const std::string& key,
                                                  const std::string& content) {
        int64_t value = 0;
        const auto* begin = content.data();
        const auto* end = content.data() + content.size();
        auto [ptr, ec] = std::from_chars(begin, end, value);
        if (ec != std::errc() || ptr != end) {
            LOG_WARN << "Invalid integer value from Redis key '" << key << "': " << content;
            return std::nullopt;
        }

        return value;
    }

public:
    CacheManager()
        : userSessionTtl_(3600)    // 1小时
        , userMenusTtl_(1800)      // 30分钟
        , userRolesTtl_(3600)      // 1小时
    {
        const auto& config = app().getCustomConfig();
        if (config.isMember("cache")) {
            userSessionTtl_ = config["cache"].get("user_session_ttl", 3600).asInt();
            userMenusTtl_ = config["cache"].get("user_menus_ttl", 1800).asInt();
            userRolesTtl_ = config["cache"].get("user_roles_ttl", 3600).asInt();
        }
    }

    // ==================== 用户会话缓存 ====================

    /**
     * @brief 缓存用户会话信息
     */
    Task<bool> cacheUserSession(int userId, const SystemHelpers::CurrentUserSummary& userInfo) {
        std::string key = "session:user:" + std::to_string(userId);
        co_return co_await redis_.set(key, serializeJson(SystemHelpers::currentUserToJson(userInfo)), userSessionTtl_);
    }

    /**
     * @brief 获取用户会话信息
     */
    Task<std::optional<SystemHelpers::CurrentUserSummary>> getUserSession(int userId) {
        std::string key = "session:user:" + std::to_string(userId);
        auto content = co_await redis_.get(key);
        if (!content) {
            co_return std::nullopt;
        }

        auto userInfo = parseJson(key, *content);
        if (!userInfo) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::currentUserFromJson(*userInfo);
    }

    /**
     * @brief 删除用户会话（用户登出、权限变更时调用）
     */
    Task<bool> deleteUserSession(int userId) {
        std::string key = "session:user:" + std::to_string(userId);
        co_return co_await redis_.del(key);
    }

    // ==================== 用户角色缓存 ====================

    /**
     * @brief 缓存用户角色
     */
    Task<bool> cacheUserRoles(int userId, const std::vector<SystemHelpers::RoleSummary>& roles) {
        std::string key = "user:roles:" + std::to_string(userId);
        co_return co_await redis_.set(key, serializeJson(SystemHelpers::rolesToJson(roles)), userRolesTtl_);
    }

    /**
     * @brief 获取用户角色
     */
    Task<std::optional<std::vector<SystemHelpers::RoleSummary>>> getUserRoles(int userId) {
        std::string key = "user:roles:" + std::to_string(userId);
        auto content = co_await redis_.get(key);
        if (!content) {
            co_return std::nullopt;
        }

        auto roles = parseJson(key, *content);
        if (!roles) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::rolesFromJson(*roles);
    }

    /**
     * @brief 删除用户角色缓存
     */
    Task<bool> deleteUserRoles(int userId) {
        std::string key = "user:roles:" + std::to_string(userId);
        co_return co_await redis_.del(key);
    }

    // ==================== 用户菜单缓存 ====================

    /**
     * @brief 缓存用户菜单
     */
    Task<bool> cacheUserMenus(int userId, const std::vector<SystemHelpers::MenuSummary>& menus) {
        std::string key = "user:menus:" + std::to_string(userId);
        co_return co_await redis_.set(key, serializeJson(SystemHelpers::menusToJson(menus)), userMenusTtl_);
    }

    /**
     * @brief 获取用户菜单
     */
    Task<std::optional<std::vector<SystemHelpers::MenuSummary>>> getUserMenus(int userId) {
        std::string key = "user:menus:" + std::to_string(userId);
        auto content = co_await redis_.get(key);
        if (!content) {
            co_return std::nullopt;
        }

        auto menus = parseJson(key, *content);
        if (!menus) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::menusFromJson(*menus);
    }

    /**
     * @brief 删除用户菜单缓存
     */
    Task<bool> deleteUserMenus(int userId) {
        std::string key = "user:menus:" + std::to_string(userId);
        co_return co_await redis_.del(key);
    }

    // ==================== 全局菜单缓存 ====================

    /**
     * @brief 缓存所有菜单（超级管理员使用）
     */
    Task<bool> cacheAllMenus(const std::vector<SystemHelpers::MenuSummary>& menus) {
        co_return co_await redis_.set("menu:all", serializeJson(SystemHelpers::menusToJson(menus)), userMenusTtl_);
    }

    /**
     * @brief 获取所有菜单
     */
    Task<std::optional<std::vector<SystemHelpers::MenuSummary>>> getAllMenus() {
        auto content = co_await redis_.get("menu:all");
        if (!content) {
            co_return std::nullopt;
        }

        auto menus = parseJson("menu:all", *content);
        if (!menus) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::menusFromJson(*menus);
    }

    /**
     * @brief 删除所有菜单缓存（菜单更新时调用）
     */
    Task<bool> deleteAllMenus() {
        co_return co_await redis_.del("menu:all");
    }

    // ==================== Token 黑名单 ====================

    /**
     * @brief 将 Token 加入黑名单（用于强制登出）
     */
    Task<bool> blacklistToken(const std::string& token, int ttl) {
        std::string key = "blacklist:token:" + token;
        co_return co_await redis_.set(key, "1", ttl);
    }

    /**
     * @brief 检查 Token 是否在黑名单中
     */
    Task<bool> isTokenBlacklisted(const std::string& token) {
        std::string key = "blacklist:token:" + token;
        co_return co_await redis_.exists(key);
    }

    // ==================== 登录失败限流 ====================

    /**
     * @brief 记录登录失败次数
     * @return 失败次数
     */
    Task<int64_t> recordLoginFailure(const std::string& username) {
        std::string key = "login:failed:" + username;
        co_return co_await redis_.incrWithExpire(key, 900); // 15分钟
    }

    /**
     * @brief 清除登录失败记录
     */
    Task<bool> clearLoginFailure(const std::string& username) {
        std::string key = "login:failed:" + username;
        co_return co_await redis_.del(key);
    }

    /**
     * @brief 获取登录失败次数
     */
    Task<int64_t> getLoginFailureCount(const std::string& username) {
        std::string key = "login:failed:" + username;
        auto value = co_await redis_.get(key);
        if (!value) {
            co_return 0;
        }

        auto parsed = parseInt64Value(key, *value);
        if (!parsed) {
            co_return 0;
        }

        co_return *parsed;
    }

    // ==================== API 限流 ====================

    /**
     * @brief 检查 API 访问频率
     * @return 当前访问次数
     */
    Task<int64_t> checkRateLimit(int userId, const std::string& endpoint, int maxRequests, int windowSeconds) {
        std::string key = "ratelimit:" + std::to_string(userId) + ":" + endpoint;
        auto count = co_await redis_.incrWithExpire(key, windowSeconds);
        if (maxRequests > 0 && count > maxRequests) {
            LOG_WARN << "Rate limit exceeded for key '" << key << "': "
                     << count << "/" << maxRequests;
        }
        co_return count;
    }

    // ==================== 批量清除缓存 ====================

    /**
     * @brief 清除用户所有缓存（权限变更、角色变更时调用）
     */
    Task<void> clearUserCache(int userId) {
        co_await deleteUserSession(userId);
        co_await deleteUserRoles(userId);
        co_await deleteUserMenus(userId);
        LOG_INFO << "Cleared all cache for user: " << userId;
    }

    /**
     * @brief 清除所有用户的菜单缓存（菜单变更时调用）
     */
    Task<int> clearAllUserMenusCache() {
        co_await deleteAllMenus();
        auto count = co_await redis_.delPattern("user:menus:*");
        LOG_INFO << "Cleared menu cache for " << count << " users";
        co_return count;
    }

    /**
     * @brief 清除所有用户的角色缓存（角色权限变更时调用）
     */
    Task<int> clearAllUserRolesCache() {
        auto count = co_await redis_.delPattern("user:roles:*");
        LOG_INFO << "Cleared role cache for " << count << " users";
        co_return count;
    }
};
