#pragma once

#include <charconv>
#include <json/json.h>

#include "common/database/RedisService.hpp"
#include "modules/home/HomeHelpers.hpp"
#include "modules/system/SystemHelpers.hpp"
#include <sstream>
#include <string>
#include <optional>
#include <vector>

/**
 * @brief 缓存管理器
 * 提供业务层缓存操作，统一管理缓存键命名和 TTL
 */
class CacheManager {
private:
    struct CacheVersionState {
        int64_t userVersion{0};
        int64_t authzVersion{0};
    };

    RedisService redis_;
    int userSessionTtl_;
    int userRecordsTtl_;
    int userMenusTtl_;
    int userRolesTtl_;
    int homeStatsTtl_;

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

    static std::string userVersionKey(int userId) {
        return "cache:version:user:" + std::to_string(userId);
    }

    static std::string authzVersionKey() {
        return "cache:version:authz";
    }

    static std::string userRecordsVersionKey() {
        return "cache:version:userrecords";
    }

    static std::string roleRecordsVersionKey() {
        return "cache:version:rolerecords";
    }

    static std::string departmentVersionKey() {
        return "cache:version:department";
    }

    static std::string homeVersionKey() {
        return "cache:version:home";
    }

    static std::string buildUserScopedKey(const std::string& prefix,
                                          int userId,
                                          const CacheVersionState& versions) {
        return prefix + ":" + std::to_string(userId) + ":u" +
               std::to_string(versions.userVersion) + ":a" +
               std::to_string(versions.authzVersion);
    }

    static std::string buildAuthzScopedKey(const std::string& prefix,
                                           int64_t authzVersion) {
        return prefix + ":a" + std::to_string(authzVersion);
    }

    static std::string buildAuthzScopedKey(const std::string& prefix,
                                           int entityId,
                                           int64_t authzVersion) {
        return prefix + ":" + std::to_string(entityId) + ":a" + std::to_string(authzVersion);
    }

    Task<CacheVersionState> getCacheVersions(int userId) {
        CacheVersionState versions;

        auto values = co_await redis_.mget({userVersionKey(userId), authzVersionKey()});
        if (values.size() > 0 && values[0]) {
            auto parsed = parseInt64Value(userVersionKey(userId), *values[0]);
            if (parsed) {
                versions.userVersion = *parsed;
            }
        }
        if (values.size() > 1 && values[1]) {
            auto parsed = parseInt64Value(authzVersionKey(), *values[1]);
            if (parsed) {
                versions.authzVersion = *parsed;
            }
        }

        co_return versions;
    }

    Task<int64_t> bumpUserVersion(int userId) {
        co_return co_await redis_.incr(userVersionKey(userId));
    }

    Task<int64_t> bumpAuthzVersion() {
        co_return co_await redis_.incr(authzVersionKey());
    }

    Task<int64_t> bumpUserRecordsVersion() {
        co_return co_await redis_.incr(userRecordsVersionKey());
    }

    Task<int64_t> bumpRoleRecordsVersion() {
        co_return co_await redis_.incr(roleRecordsVersionKey());
    }

    Task<int64_t> bumpDepartmentVersion() {
        co_return co_await redis_.incr(departmentVersionKey());
    }

    Task<int64_t> bumpHomeVersion() {
        co_return co_await redis_.incr(homeVersionKey());
    }

    Task<std::string> buildUserScopedKey(const std::string& prefix, int userId) {
        auto versions = co_await getCacheVersions(userId);
        co_return buildUserScopedKey(prefix, userId, versions);
    }

    Task<std::string> buildAuthzScopedKey(const std::string& prefix) {
        auto authzVersion = co_await redis_.get(authzVersionKey());
        int64_t version = 0;
        if (authzVersion) {
            auto parsed = parseInt64Value(authzVersionKey(), *authzVersion);
            if (parsed) {
                version = *parsed;
            }
        }
        co_return buildAuthzScopedKey(prefix, version);
    }

    Task<bool> setUserVersionedValue(const std::string& prefix,
                                     int userId,
                                     const std::string& value,
                                     int ttl) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return false;
        }

        try {
            const std::string userKey = userVersionKey(userId);
            const std::string authzKey = authzVersionKey();
            const std::string userIdText = std::to_string(userId);
            const std::string ttlText = std::to_string(ttl);
            static constexpr const char* script = R"(
                local userVersion = redis.call('GET', KEYS[1])
                if not userVersion then userVersion = '0' end
                local authzVersion = redis.call('GET', KEYS[2])
                if not authzVersion then authzVersion = '0' end
                local key = ARGV[1] .. ':' .. ARGV[2] .. ':u' .. userVersion .. ':a' .. authzVersion
                return redis.call('SETEX', key, ARGV[3], ARGV[4])
            )";

            co_await client->execCommandCoro(
                "EVAL %s 2 %s %s %s %s %s %s",
                script,
                userKey.c_str(),
                authzKey.c_str(),
                prefix.c_str(),
                userIdText.c_str(),
                ttlText.c_str(),
                value.c_str());
            co_return true;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis versioned SET error for prefix '" << prefix << "': " << e.what();
            co_return false;
        }
    }

    Task<bool> setAuthzVersionedValue(const std::string& prefix,
                                      const std::string& value,
                                      int ttl) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return false;
        }

        try {
            const std::string authzKey = authzVersionKey();
            const std::string ttlText = std::to_string(ttl);
            static constexpr const char* script = R"(
                local authzVersion = redis.call('GET', KEYS[1])
                if not authzVersion then authzVersion = '0' end
                local key = ARGV[1] .. ':a' .. authzVersion
                return redis.call('SETEX', key, ARGV[2], ARGV[3])
            )";

            co_await client->execCommandCoro(
                "EVAL %s 1 %s %s %s %s",
                script,
                authzKey.c_str(),
                prefix.c_str(),
                ttlText.c_str(),
                value.c_str());
            co_return true;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis authz versioned SET error for prefix '" << prefix << "': " << e.what();
            co_return false;
        }
    }

    Task<std::optional<std::string>> getUserVersionedValue(int userId, const std::string& prefix) {
        if (!AppRedisConfig::enabled()) {
            co_return std::nullopt;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return std::nullopt;
        }

        try {
            const std::string userKey = userVersionKey(userId);
            const std::string authzKey = authzVersionKey();
            const std::string userIdText = std::to_string(userId);
            static constexpr const char* script = R"(
                local userVersion = redis.call('GET', KEYS[1])
                if not userVersion then userVersion = '0' end
                local authzVersion = redis.call('GET', KEYS[2])
                if not authzVersion then authzVersion = '0' end
                local key = ARGV[1] .. ':' .. ARGV[2] .. ':u' .. userVersion .. ':a' .. authzVersion
                return redis.call('GET', key)
            )";

            auto result = co_await client->execCommandCoro(
                "EVAL %s 2 %s %s %s %s",
                script,
                userKey.c_str(),
                authzKey.c_str(),
                prefix.c_str(),
                userIdText.c_str());

            if (result.isNil()) {
                co_return std::nullopt;
            }

            co_return result.asString();
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis versioned GET error for prefix '" << prefix << "': " << e.what();
            co_return std::nullopt;
        }
    }

    Task<std::optional<std::string>> getAuthzVersionedValue(const std::string& prefix) {
        if (!AppRedisConfig::enabled()) {
            co_return std::nullopt;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return std::nullopt;
        }

        try {
            const std::string authzKey = authzVersionKey();
            static constexpr const char* script = R"(
                local authzVersion = redis.call('GET', KEYS[1])
                if not authzVersion then authzVersion = '0' end
                local key = ARGV[1] .. ':a' .. authzVersion
                return redis.call('GET', key)
            )";

            auto result = co_await client->execCommandCoro(
                "EVAL %s 1 %s %s",
                script,
                authzKey.c_str(),
                prefix.c_str());

            if (result.isNil()) {
                co_return std::nullopt;
            }

            co_return result.asString();
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis versioned authz GET error for prefix '" << prefix << "': " << e.what();
            co_return std::nullopt;
        }
    }

    Task<bool> setDepartmentVersionedValue(const std::string& prefix,
                                           const std::string& value,
                                           int ttl) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return false;
        }

        try {
            const std::string versionKey = departmentVersionKey();
            const std::string ttlText = std::to_string(ttl);
            static constexpr const char* script = R"(
                local version = redis.call('GET', KEYS[1])
                if not version then version = '0' end
                local key = ARGV[1] .. ':a' .. version
                return redis.call('SETEX', key, ARGV[2], ARGV[3])
            )";

            co_await client->execCommandCoro(
                "EVAL %s 1 %s %s %s %s",
                script,
                versionKey.c_str(),
                prefix.c_str(),
                ttlText.c_str(),
                value.c_str());
            co_return true;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis department versioned SET error for prefix '" << prefix << "': " << e.what();
            co_return false;
        }
    }

    Task<std::optional<std::string>> getDepartmentVersionedValue(const std::string& prefix) {
        if (!AppRedisConfig::enabled()) {
            co_return std::nullopt;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return std::nullopt;
        }

        try {
            const std::string versionKey = departmentVersionKey();
            static constexpr const char* script = R"(
                local version = redis.call('GET', KEYS[1])
                if not version then version = '0' end
                local key = ARGV[1] .. ':a' .. version
                return redis.call('GET', key)
            )";

            auto result = co_await client->execCommandCoro(
                "EVAL %s 1 %s %s",
                script,
                versionKey.c_str(),
                prefix.c_str());

            if (result.isNil()) {
                co_return std::nullopt;
            }

            co_return result.asString();
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis department versioned GET error for prefix '" << prefix << "': " << e.what();
            co_return std::nullopt;
        }
    }

    Task<bool> setUserRecordsVersionedValue(const std::string& prefix,
                                            const std::string& value,
                                            int ttl) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return false;
        }

        try {
            const std::string versionKey = userRecordsVersionKey();
            const std::string ttlText = std::to_string(ttl);
            static constexpr const char* script = R"(
                local version = redis.call('GET', KEYS[1])
                if not version then version = '0' end
                local key = ARGV[1] .. ':a' .. version
                return redis.call('SETEX', key, ARGV[2], ARGV[3])
            )";

            co_await client->execCommandCoro(
                "EVAL %s 1 %s %s %s %s",
                script,
                versionKey.c_str(),
                prefix.c_str(),
                ttlText.c_str(),
                value.c_str());
            co_return true;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis user records versioned SET error for prefix '" << prefix << "': " << e.what();
            co_return false;
        }
    }

    Task<std::optional<std::string>> getUserRecordsVersionedValue(const std::string& prefix) {
        if (!AppRedisConfig::enabled()) {
            co_return std::nullopt;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return std::nullopt;
        }

        try {
            const std::string versionKey = userRecordsVersionKey();
            static constexpr const char* script = R"(
                local version = redis.call('GET', KEYS[1])
                if not version then version = '0' end
                local key = ARGV[1] .. ':a' .. version
                return redis.call('GET', key)
            )";

            auto result = co_await client->execCommandCoro(
                "EVAL %s 1 %s %s",
                script,
                versionKey.c_str(),
                prefix.c_str());

            if (result.isNil()) {
                co_return std::nullopt;
            }

            co_return result.asString();
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis user records versioned GET error for prefix '" << prefix << "': " << e.what();
            co_return std::nullopt;
        }
    }

    Task<bool> setRoleRecordsVersionedValue(const std::string& prefix,
                                            const std::string& value,
                                            int ttl) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return false;
        }

        try {
            const std::string versionKey = roleRecordsVersionKey();
            const std::string ttlText = std::to_string(ttl);
            static constexpr const char* script = R"(
                local version = redis.call('GET', KEYS[1])
                if not version then version = '0' end
                local key = ARGV[1] .. ':a' .. version
                return redis.call('SETEX', key, ARGV[2], ARGV[3])
            )";

            co_await client->execCommandCoro(
                "EVAL %s 1 %s %s %s %s",
                script,
                versionKey.c_str(),
                prefix.c_str(),
                ttlText.c_str(),
                value.c_str());
            co_return true;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis role records versioned SET error for prefix '" << prefix << "': " << e.what();
            co_return false;
        }
    }

    Task<std::optional<std::string>> getRoleRecordsVersionedValue(const std::string& prefix) {
        if (!AppRedisConfig::enabled()) {
            co_return std::nullopt;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return std::nullopt;
        }

        try {
            const std::string versionKey = roleRecordsVersionKey();
            static constexpr const char* script = R"(
                local version = redis.call('GET', KEYS[1])
                if not version then version = '0' end
                local key = ARGV[1] .. ':a' .. version
                return redis.call('GET', key)
            )";

            auto result = co_await client->execCommandCoro(
                "EVAL %s 1 %s %s",
                script,
                versionKey.c_str(),
                prefix.c_str());

            if (result.isNil()) {
                co_return std::nullopt;
            }

            co_return result.asString();
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis role records versioned GET error for prefix '" << prefix << "': " << e.what();
            co_return std::nullopt;
        }
    }

    Task<bool> setHomeVersionedValue(const std::string& prefix,
                                     const std::string& value,
                                     int ttl) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return false;
        }

        try {
            const std::string versionKey = homeVersionKey();
            const std::string ttlText = std::to_string(ttl);
            static constexpr const char* script = R"(
                local version = redis.call('GET', KEYS[1])
                if not version then version = '0' end
                local key = ARGV[1] .. ':a' .. version
                return redis.call('SETEX', key, ARGV[2], ARGV[3])
            )";

            co_await client->execCommandCoro(
                "EVAL %s 1 %s %s %s %s",
                script,
                versionKey.c_str(),
                prefix.c_str(),
                ttlText.c_str(),
                value.c_str());
            co_return true;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis home versioned SET error for prefix '" << prefix << "': " << e.what();
            co_return false;
        }
    }

    Task<std::optional<std::string>> getHomeVersionedValue(const std::string& prefix) {
        if (!AppRedisConfig::enabled()) {
            co_return std::nullopt;
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return std::nullopt;
        }

        try {
            const std::string versionKey = homeVersionKey();
            static constexpr const char* script = R"(
                local version = redis.call('GET', KEYS[1])
                if not version then version = '0' end
                local key = ARGV[1] .. ':a' .. version
                return redis.call('GET', key)
            )";

            auto result = co_await client->execCommandCoro(
                "EVAL %s 1 %s %s",
                script,
                versionKey.c_str(),
                prefix.c_str());

            if (result.isNil()) {
                co_return std::nullopt;
            }

            co_return result.asString();
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis home versioned GET error for prefix '" << prefix << "': " << e.what();
            co_return std::nullopt;
        }
    }

public:
    CacheManager()
        : userSessionTtl_(3600)    // 1小时
        , userRecordsTtl_(300)     // 5分钟
        , userMenusTtl_(1800)      // 30分钟
        , userRolesTtl_(3600)      // 1小时
        , homeStatsTtl_(300)       // 5分钟
    {
        const auto& config = app().getCustomConfig();
        if (config.isMember("cache")) {
            userSessionTtl_ = config["cache"].get("user_session_ttl", 3600).asInt();
            userRecordsTtl_ = config["cache"].get("user_records_ttl", 300).asInt();
            userMenusTtl_ = config["cache"].get("user_menus_ttl", 1800).asInt();
            userRolesTtl_ = config["cache"].get("user_roles_ttl", 3600).asInt();
            homeStatsTtl_ = config["cache"].get("home_stats_ttl", 300).asInt();
        }
    }

    // ==================== 用户会话缓存 ====================

    /**
     * @brief 缓存用户会话信息
     */
    Task<bool> cacheUserSession(int userId, const SystemHelpers::CurrentUserSummary& userInfo) {
        co_return co_await setUserVersionedValue(
            "session:user",
            userId,
            serializeJson(SystemHelpers::currentUserToJson(userInfo)),
            userSessionTtl_);
    }

    /**
     * @brief 获取用户会话信息
     */
    Task<std::optional<SystemHelpers::CurrentUserSummary>> getUserSession(int userId) {
        auto content = co_await getUserVersionedValue(userId, "session:user");
        if (!content) {
            co_return std::nullopt;
        }

        std::string key = "session:user:" + std::to_string(userId);
        auto userInfo = parseJson(key, *content);
        if (!userInfo) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::currentUserFromJson(*userInfo);
    }

    // ==================== 用户记录缓存 ====================

    /**
     * @brief 缓存用户记录列表（用于管理页）
     */
    Task<bool> cacheUserRecords(const std::vector<SystemHelpers::UserRecordSummary>& users) {
        co_return co_await setUserRecordsVersionedValue(
            "user:records",
            serializeJson(SystemHelpers::userRecordItemsToJson(users)),
            userRecordsTtl_);
    }

    /**
     * @brief 获取用户记录列表（用于管理页）
     */
    Task<std::optional<std::vector<SystemHelpers::UserRecordSummary>>> getUserRecords() {
        auto content = co_await getUserRecordsVersionedValue("user:records");
        if (!content) {
            co_return std::nullopt;
        }

        auto json = parseJson("user:records", *content);
        if (!json) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::userRecordItemsFromJson(*json);
    }

    // ==================== 角色记录缓存 ====================

    /**
     * @brief 缓存角色记录列表（用于管理页）
     */
    Task<bool> cacheRoleRecords(const std::vector<SystemHelpers::RoleRecordSummary>& roles) {
        co_return co_await setRoleRecordsVersionedValue(
            "role:records",
            serializeJson(SystemHelpers::roleRecordItemsToJson(roles)),
            userRolesTtl_);
    }

    /**
     * @brief 获取角色记录列表（用于管理页）
     */
    Task<std::optional<std::vector<SystemHelpers::RoleRecordSummary>>> getRoleRecords() {
        auto content = co_await getRoleRecordsVersionedValue("role:records");
        if (!content) {
            co_return std::nullopt;
        }

        auto json = parseJson("role:records", *content);
        if (!json) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::roleRecordItemsFromJson(*json);
    }

    // ==================== 用户角色缓存 ====================

    /**
     * @brief 缓存用户角色
     */
    Task<bool> cacheUserRoles(int userId, const std::vector<SystemHelpers::RoleSummary>& roles) {
        co_return co_await setUserVersionedValue(
            "user:roles",
            userId,
            serializeJson(SystemHelpers::rolesToJson(roles)),
            userRolesTtl_);
    }

    /**
     * @brief 获取用户角色
     */
    Task<std::optional<std::vector<SystemHelpers::RoleSummary>>> getUserRoles(int userId) {
        auto content = co_await getUserVersionedValue(userId, "user:roles");
        if (!content) {
            co_return std::nullopt;
        }

        std::string key = "user:roles:" + std::to_string(userId);
        auto roles = parseJson(key, *content);
        if (!roles) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::rolesFromJson(*roles);
    }

    Task<std::vector<std::optional<std::vector<SystemHelpers::RoleSummary>>>> getUserRolesBatch(
        const std::vector<int>& userIds) {
        if (!AppRedisConfig::enabled() || userIds.empty()) {
            co_return {};
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return std::vector<std::optional<std::vector<SystemHelpers::RoleSummary>>>(userIds.size());
        }

        try {
            std::vector<std::string> versionKeys;
            versionKeys.reserve(userIds.size() + 1);
            for (int userId : userIds) {
                versionKeys.push_back(userVersionKey(userId));
            }
            versionKeys.push_back(authzVersionKey());

            auto versions = co_await redis_.mget(versionKeys);

            int64_t authzVersion = 0;
            if (versions.size() > userIds.size() && versions.back()) {
                auto parsed = parseInt64Value(authzVersionKey(), *versions.back());
                if (parsed) {
                    authzVersion = *parsed;
                }
            }

            std::vector<std::string> roleKeys;
            roleKeys.reserve(userIds.size());

            for (size_t i = 0; i < userIds.size(); ++i) {
                CacheVersionState versionsState;
                if (i < versions.size() && versions[i]) {
                    auto parsed = parseInt64Value(userVersionKey(userIds[i]), *versions[i]);
                    if (parsed) {
                        versionsState.userVersion = *parsed;
                    }
                }
                versionsState.authzVersion = authzVersion;
                roleKeys.push_back(buildUserScopedKey("user:roles", userIds[i], versionsState));
            }

            auto contents = co_await redis_.mget(roleKeys);

            std::vector<std::optional<std::vector<SystemHelpers::RoleSummary>>> output;
            output.reserve(userIds.size());
            for (size_t i = 0; i < userIds.size(); ++i) {
                if (i < contents.size() && contents[i]) {
                    auto key = "user:roles:" + std::to_string(userIds[i]);
                    auto roles = parseJson(key, *contents[i]);
                    if (roles) {
                        output.emplace_back(SystemHelpers::rolesFromJson(*roles));
                        continue;
                    }
                }
                output.emplace_back(std::nullopt);
            }

            co_return output;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis versioned batch GET error for user roles: " << e.what();
            co_return std::vector<std::optional<std::vector<SystemHelpers::RoleSummary>>>(userIds.size());
        }
    }

    /**
     * @brief 缓存用户角色 ID
     */
    Task<bool> cacheUserRoleIds(int userId, const std::vector<int>& roleIds) {
        co_return co_await setUserVersionedValue(
            "user:roleIds",
            userId,
            serializeJson(SystemHelpers::intArrayToJson(roleIds)),
            userRolesTtl_);
    }

    /**
     * @brief 获取用户角色 ID
     */
    Task<std::optional<std::vector<int>>> getUserRoleIds(int userId) {
        auto content = co_await getUserVersionedValue(userId, "user:roleIds");
        if (!content) {
            co_return std::nullopt;
        }

        std::string key = "user:roleIds:" + std::to_string(userId);
        auto json = parseJson(key, *content);
        if (!json) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::fromJsonArray<int>(*json, [](const Json::Value& item) {
            return item.asInt();
        });
    }

    /**
     * @brief 批量获取用户角色 ID
     */
    Task<std::vector<std::optional<std::vector<int>>>> getUserRoleIdsBatch(const std::vector<int>& userIds) {
        if (!AppRedisConfig::enabled() || userIds.empty()) {
            co_return {};
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return std::vector<std::optional<std::vector<int>>>(userIds.size());
        }

        try {
            std::vector<std::string> versionKeys;
            versionKeys.reserve(userIds.size() + 1);
            for (int userId : userIds) {
                versionKeys.push_back(userVersionKey(userId));
            }
            versionKeys.push_back(authzVersionKey());

            auto versions = co_await redis_.mget(versionKeys);

            int64_t authzVersion = 0;
            if (versions.size() > userIds.size() && versions.back()) {
                auto parsed = parseInt64Value(authzVersionKey(), *versions.back());
                if (parsed) {
                    authzVersion = *parsed;
                }
            }

            std::vector<std::string> roleIdKeys;
            roleIdKeys.reserve(userIds.size());
            for (size_t i = 0; i < userIds.size(); ++i) {
                CacheVersionState versionsState;
                if (i < versions.size() && versions[i]) {
                    auto parsed = parseInt64Value(userVersionKey(userIds[i]), *versions[i]);
                    if (parsed) {
                        versionsState.userVersion = *parsed;
                    }
                }
                versionsState.authzVersion = authzVersion;
                roleIdKeys.push_back(buildUserScopedKey("user:roleIds", userIds[i], versionsState));
            }

            auto contents = co_await redis_.mget(roleIdKeys);

            std::vector<std::optional<std::vector<int>>> output;
            output.reserve(userIds.size());
            for (size_t i = 0; i < userIds.size(); ++i) {
                if (i < contents.size() && contents[i]) {
                    auto key = "user:roleIds:" + std::to_string(userIds[i]);
                    auto json = parseJson(key, *contents[i]);
                    if (json) {
                        output.emplace_back(SystemHelpers::fromJsonArray<int>(*json, [](const Json::Value& item) {
                            return item.asInt();
                        }));
                        continue;
                    }
                }
                output.emplace_back(std::nullopt);
            }

            co_return output;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis batch user role ID GET error: " << e.what();
            co_return std::vector<std::optional<std::vector<int>>>(userIds.size());
        }
    }

    // ==================== 角色列表缓存 ====================

    /**
     * @brief 缓存所有启用角色（用于角色下拉选择）
     */
    Task<bool> cacheAllRoles(const std::vector<SystemHelpers::RoleSummary>& roles) {
        co_return co_await setAuthzVersionedValue(
            "role:all",
            serializeJson(SystemHelpers::rolesToJson(roles)),
            userRolesTtl_);
    }

    /**
     * @brief 获取所有启用角色（用于角色下拉选择）
     */
    Task<std::optional<std::vector<SystemHelpers::RoleSummary>>> getAllRoles() {
        auto content = co_await getAuthzVersionedValue("role:all");
        if (!content) {
            co_return std::nullopt;
        }

        auto json = parseJson("role:all", *content);
        if (!json) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::rolesFromJson(*json);
    }

    /**
     * @brief 缓存角色菜单 ID
     */
    Task<bool> cacheRoleMenuIds(int roleId, const std::vector<int>& menuIds) {
        co_return co_await setAuthzVersionedValue(
            "role:menuIds:" + std::to_string(roleId),
            serializeJson(SystemHelpers::intArrayToJson(menuIds)),
            userRolesTtl_);
    }

    /**
     * @brief 获取角色菜单 ID
     */
    Task<std::optional<std::vector<int>>> getRoleMenuIds(int roleId) {
        auto content = co_await getAuthzVersionedValue("role:menuIds:" + std::to_string(roleId));
        if (!content) {
            co_return std::nullopt;
        }

        std::string key = "role:menuIds:" + std::to_string(roleId);
        auto json = parseJson(key, *content);
        if (!json) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::fromJsonArray<int>(*json, [](const Json::Value& item) {
            return item.asInt();
        });
    }

    /**
     * @brief 批量获取角色菜单 ID
     */
    Task<std::vector<std::optional<std::vector<int>>>> getRoleMenuIdsBatch(const std::vector<int>& roleIds) {
        if (!AppRedisConfig::enabled() || roleIds.empty()) {
            co_return {};
        }

        auto client = redis_.getClient();
        if (!client) {
            co_return std::vector<std::optional<std::vector<int>>>(roleIds.size());
        }

        try {
            const std::string authzKey = authzVersionKey();
            auto authzVersion = co_await redis_.get(authzKey);
            int64_t version = 0;
            if (authzVersion) {
                auto parsed = parseInt64Value(authzKey, *authzVersion);
                if (parsed) {
                    version = *parsed;
                }
            }

            std::vector<std::string> keys;
            keys.reserve(roleIds.size());
            for (int roleId : roleIds) {
                keys.push_back(buildAuthzScopedKey("role:menuIds", roleId, version));
            }

            auto contents = co_await redis_.mget(keys);
            std::vector<std::optional<std::vector<int>>> output;
            output.reserve(roleIds.size());
            for (size_t i = 0; i < roleIds.size(); ++i) {
                if (i < contents.size() && contents[i]) {
                    auto json = parseJson(keys[i], *contents[i]);
                    if (json) {
                        output.emplace_back(SystemHelpers::fromJsonArray<int>(*json, [](const Json::Value& item) {
                            return item.asInt();
                        }));
                        continue;
                    }
                }
                output.emplace_back(std::nullopt);
            }

            co_return output;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis batch role menu ID GET error: " << e.what();
            co_return std::vector<std::optional<std::vector<int>>>(roleIds.size());
        }
    }

    // ==================== 菜单记录缓存 ====================

    /**
     * @brief 缓存菜单记录列表（用于管理页）
     */
    Task<bool> cacheMenuRecords(const std::vector<SystemHelpers::MenuRecordSummary>& menus) {
        co_return co_await setAuthzVersionedValue(
            "menu:records",
            serializeJson(SystemHelpers::menuRecordItemsToJson(menus)),
            userMenusTtl_);
    }

    /**
     * @brief 获取菜单记录列表（用于管理页）
     */
    Task<std::optional<std::vector<SystemHelpers::MenuRecordSummary>>> getMenuRecords() {
        auto content = co_await getAuthzVersionedValue("menu:records");
        if (!content) {
            co_return std::nullopt;
        }

        auto json = parseJson("menu:records", *content);
        if (!json) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::menuRecordItemsFromJson(*json);
    }

    // ==================== 部门记录缓存 ====================

    /**
     * @brief 缓存部门记录列表（用于管理页）
     */
    Task<bool> cacheDepartmentRecords(const std::vector<SystemHelpers::DepartmentRecordSummary>& departments) {
        co_return co_await setDepartmentVersionedValue(
            "department:records",
            serializeJson(SystemHelpers::departmentRecordItemsToJson(departments)),
            userMenusTtl_);
    }

    /**
     * @brief 获取部门记录列表（用于管理页）
     */
    Task<std::optional<std::vector<SystemHelpers::DepartmentRecordSummary>>> getDepartmentRecords() {
        auto content = co_await getDepartmentVersionedValue("department:records");
        if (!content) {
            co_return std::nullopt;
        }

        auto json = parseJson("department:records", *content);
        if (!json) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::departmentRecordItemsFromJson(*json);
    }

    // ==================== 首页统计缓存 ====================

    /**
     * @brief 缓存首页统计
     */
    Task<bool> cacheHomeStats(const HomeHelpers::HomeStatsSummary& stats) {
        co_return co_await setHomeVersionedValue(
            "home:stats",
            serializeJson(HomeHelpers::homeStatsToJson(stats)),
            homeStatsTtl_);
    }

    /**
     * @brief 获取首页统计
     */
    Task<std::optional<HomeHelpers::HomeStatsSummary>> getHomeStats() {
        auto content = co_await getHomeVersionedValue("home:stats");
        if (!content) {
            co_return std::nullopt;
        }

        auto json = parseJson("home:stats", *content);
        if (!json) {
            co_return std::nullopt;
        }

        co_return HomeHelpers::homeStatsFromJson(*json);
    }

    // ==================== 用户菜单缓存 ====================

    /**
     * @brief 缓存用户菜单
     */
    Task<bool> cacheUserMenus(int userId, const std::vector<SystemHelpers::MenuSummary>& menus) {
        co_return co_await setUserVersionedValue(
            "user:menus",
            userId,
            serializeJson(SystemHelpers::menusToJson(menus)),
            userMenusTtl_);
    }

    /**
     * @brief 获取用户菜单
     */
    Task<std::optional<std::vector<SystemHelpers::MenuSummary>>> getUserMenus(int userId) {
        auto content = co_await getUserVersionedValue(userId, "user:menus");
        if (!content) {
            co_return std::nullopt;
        }

        std::string key = "user:menus:" + std::to_string(userId);
        auto menus = parseJson(key, *content);
        if (!menus) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::menusFromJson(*menus);
    }

    // ==================== 全局菜单缓存 ====================

    /**
     * @brief 缓存所有菜单（超级管理员使用）
     */
    Task<bool> cacheAllMenus(const std::vector<SystemHelpers::MenuSummary>& menus) {
        co_return co_await setAuthzVersionedValue(
            "menu:all",
            serializeJson(SystemHelpers::menusToJson(menus)),
            userMenusTtl_);
    }

    /**
     * @brief 获取所有菜单
     */
    Task<std::optional<std::vector<SystemHelpers::MenuSummary>>> getAllMenus() {
        auto content = co_await getAuthzVersionedValue("menu:all");
        if (!content) {
            co_return std::nullopt;
        }

        auto menus = parseJson("menu:all", *content);
        if (!menus) {
            co_return std::nullopt;
        }

        co_return SystemHelpers::menusFromJson(*menus);
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
    Task<int64_t> checkRateLimitKey(const std::string& scope, int maxRequests, int windowSeconds) {
        std::string key = "ratelimit:" + scope;
        auto count = co_await redis_.incrWithExpire(key, windowSeconds);
        if (maxRequests > 0 && count > maxRequests) {
            LOG_WARN << "Rate limit exceeded for key '" << key << "': "
                     << count << "/" << maxRequests;
        }
        co_return count;
    }

    Task<int64_t> checkRateLimit(int userId, const std::string& endpoint, int maxRequests, int windowSeconds) {
        co_return co_await checkRateLimitKey(std::to_string(userId) + ":" + endpoint, maxRequests, windowSeconds);
    }

    // ==================== 批量清除缓存 ====================

    /**
     * @brief 使用户相关缓存失效（通过版本号递增完成）
     */
    Task<void> clearUserCache(int userId) {
        auto version = co_await bumpUserVersion(userId);
        LOG_INFO << "Invalidated cache for user: " << userId << ", version: " << version;
    }

    /**
     * @brief 使所有授权相关缓存失效（通过版本号递增完成）
     */
    Task<void> invalidateAuthorizationCache() {
        auto version = co_await bumpAuthzVersion();
        LOG_INFO << "Invalidated authorization cache, version: " << version;
    }

    /**
     * @brief 使用户记录缓存失效（通过版本号递增完成）
     */
    Task<void> invalidateUserRecordsCache() {
        auto version = co_await bumpUserRecordsVersion();
        LOG_INFO << "Invalidated user records cache, version: " << version;
    }

    /**
     * @brief 使角色记录缓存失效（通过版本号递增完成）
     */
    Task<void> invalidateRoleRecordsCache() {
        auto version = co_await bumpRoleRecordsVersion();
        LOG_INFO << "Invalidated role records cache, version: " << version;
    }

    /**
     * @brief 使部门记录缓存失效（通过版本号递增完成）
     */
    Task<void> invalidateDepartmentCache() {
        auto version = co_await bumpDepartmentVersion();
        LOG_INFO << "Invalidated department cache, version: " << version;
    }

    /**
     * @brief 使首页统计缓存失效（通过版本号递增完成）
     */
    Task<void> invalidateHomeCache() {
        auto version = co_await bumpHomeVersion();
        LOG_INFO << "Invalidated home cache, version: " << version;
    }
};
