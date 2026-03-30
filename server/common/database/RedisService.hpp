#pragma once

#include <drogon/nosql/RedisClient.h>
#include <drogon/HttpAppFramework.h>
#include <optional>
#include <string>

using namespace drogon;
using namespace drogon::nosql;

/**
 * @brief Redis 配置（由 main.cpp 初始化）
 */
struct AppRedisConfig {
    static bool& enabled() {
        static bool value = true;
        return value;
    }

    static bool& useFast() {
        static bool value = false;
        return value;
    }
};

/**
 * @brief Redis 服务类
 * 提供统一的 Redis 访问接口，支持协程和缓存降级
 */
class RedisService {
public:
    RedisService() = default;

    /**
     * @brief 获取 Redis 客户端
     */
    [[nodiscard]]
    RedisClientPtr getClient() const {
        try {
            return AppRedisConfig::useFast()
                ? app().getFastRedisClient("default")
                : app().getRedisClient("default");
        } catch (const std::exception& e) {
            LOG_WARN << "Failed to get Redis client: " << e.what();
            return nullptr;
        }
    }

    /**
     * @brief 获取字符串值
     */
    Task<std::optional<std::string>> get(const std::string& key) {
        if (!AppRedisConfig::enabled()) {
            co_return std::nullopt;
        }

        auto client = getClient();
        if (!client) {
            co_return std::nullopt;
        }

        try {
            auto result = co_await client->execCommandCoro("GET %s", key.c_str());
            if (result.isNil()) {
                co_return std::nullopt;
            }
            co_return result.asString();
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis GET error for key '" << key << "': " << e.what();
            co_return std::nullopt;
        }
    }

    /**
     * @brief 设置字符串值（支持 TTL）
     */
    Task<bool> set(const std::string& key, const std::string& value, int ttl = 0) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = getClient();
        if (!client) {
            co_return false;
        }

        try {
            if (ttl > 0) {
                co_await client->execCommandCoro("SETEX %s %d %s",
                    key.c_str(), ttl, value.c_str());
            } else {
                co_await client->execCommandCoro("SET %s %s",
                    key.c_str(), value.c_str());
            }
            co_return true;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis SET error for key '" << key << "': " << e.what();
            co_return false;
        }
    }

    /**
     * @brief 删除键
     */
    Task<bool> del(const std::string& key) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = getClient();
        if (!client) {
            co_return false;
        }

        try {
            co_await client->execCommandCoro("DEL %s", key.c_str());
            co_return true;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis DEL error for key '" << key << "': " << e.what();
            co_return false;
        }
    }

    /**
     * @brief 删除多个键（支持通配符）
     */
    Task<int> delPattern(const std::string& pattern) {
        if (!AppRedisConfig::enabled()) {
            co_return 0;
        }

        auto client = getClient();
        if (!client) {
            co_return 0;
        }

        try {
            auto keysResult = co_await client->execCommandCoro("KEYS %s", pattern.c_str());
            if (keysResult.isNil() || keysResult.asArray().empty()) {
                co_return 0;
            }

            auto keys = keysResult.asArray();
            for (const auto& key : keys) {
                co_await del(key.asString());
            }
            co_return static_cast<int>(keys.size());
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis delPattern error for pattern '" << pattern << "': " << e.what();
            co_return 0;
        }
    }

    /**
     * @brief 检查键是否存在
     */
    Task<bool> exists(const std::string& key) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = getClient();
        if (!client) {
            co_return false;
        }

        try {
            auto result = co_await client->execCommandCoro("EXISTS %s", key.c_str());
            co_return result.asInteger() > 0;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis EXISTS error for key '" << key << "': " << e.what();
            co_return false;
        }
    }

    /**
     * @brief 设置过期时间
     */
    Task<bool> expire(const std::string& key, int seconds) {
        if (!AppRedisConfig::enabled()) {
            co_return false;
        }

        auto client = getClient();
        if (!client) {
            co_return false;
        }

        try {
            auto result = co_await client->execCommandCoro("EXPIRE %s %d",
                key.c_str(), seconds);
            co_return result.asInteger() > 0;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis EXPIRE error for key '" << key << "': " << e.what();
            co_return false;
        }
    }

    /**
     * @brief 增加计数器
     */
    Task<int64_t> incr(const std::string& key) {
        if (!AppRedisConfig::enabled()) {
            co_return 0;
        }

        auto client = getClient();
        if (!client) {
            co_return 0;
        }

        try {
            auto result = co_await client->execCommandCoro("INCR %s", key.c_str());
            co_return result.asInteger();
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis INCR error for key '" << key << "': " << e.what();
            co_return 0;
        }
    }

    /**
     * @brief 增加计数器并设置过期时间（用于限流）
     */
    Task<int64_t> incrWithExpire(const std::string& key, int ttl) {
        if (!AppRedisConfig::enabled()) {
            co_return 0;
        }

        auto client = getClient();
        if (!client) {
            co_return 0;
        }

        try {
            auto count = co_await incr(key);
            if (count == 1) {
                co_await expire(key, ttl);
            }
            co_return count;
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis incrWithExpire error for key '" << key << "': " << e.what();
            co_return 0;
        }
    }
};
