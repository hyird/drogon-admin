#pragma once

#include <drogon/HttpAppFramework.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <json/json.h>
#include "common/database/DatabaseService.hpp"
#include "common/database/RedisService.hpp"

using namespace drogon;
namespace fs = std::filesystem;

/**
 * @brief 配置管理器 - 负责加载和管理应用配置
 */
class ConfigManager {
private:
    static std::optional<bool> readFirstClientFastFlag(const Json::Value& root,
                                                       const char* key) {
        if (!root.isMember(key) || !root[key].isArray() || root[key].empty()) {
            return std::nullopt;
        }

        return root[key][0].get("is_fast", false).asBool();
    }

    static std::optional<bool> readCacheEnabledFlag(const Json::Value& root) {
        if (!root.isMember("custom_config") || !root["custom_config"].isObject()) {
            return std::nullopt;
        }

        const auto& customConfig = root["custom_config"];
        if (!customConfig.isMember("cache") || !customConfig["cache"].isObject()) {
            return std::nullopt;
        }

        return customConfig["cache"].get("enabled", true).asBool();
    }

public:
    /**
     * @brief 加载配置文件
     * @return 是否成功加载
     */
    static bool load() {
        std::vector<std::string> configPaths = {
            "./config/config.json",
            "../../config/config.json",
            "../config/config.json",
            "config.json"
        };

        for (const auto& path : configPaths) {
            if (fs::exists(path)) {
                try {
                    // 先读取配置文件获取数据库/Redis 客户端偏好和缓存开关
                    std::ifstream ifs(path);
                    if (ifs) {
                        Json::Value root;
                        Json::CharReaderBuilder builder;
                        std::string errs;
                        if (Json::parseFromStream(builder, ifs, &root, &errs)) {
                            if (auto useFast = readFirstClientFastFlag(root, "db_clients")) {
                                AppDbConfig::useFast() = *useFast;
                            }
                            if (auto useFast = readFirstClientFastFlag(root, "redis_clients")) {
                                AppRedisConfig::useFast() = *useFast;
                            }
                            if (auto cacheEnabled = readCacheEnabledFlag(root)) {
                                AppRedisConfig::enabled() = *cacheEnabled;
                            }
                        }
                    }

                    app().loadConfigFile(path);
                    LOG_INFO << "Config loaded from: " << path;
                    return true;
                } catch (const std::exception& e) {
                    LOG_WARN << "Failed to load " << path << ": " << e.what();
                }
            }
        }
        return false;
    }

    /**
     * @brief 获取日志级别配置
     */
    static std::string getLogLevel() {
        auto& config = app().getCustomConfig();
        return config.get("log_level", "INFO").asString();
    }

    /**
     * @brief 获取是否启用控制台日志
     */
    static bool isConsoleLogEnabled() {
        auto& config = app().getCustomConfig();
        return config.get("console_log", false).asBool();
    }
};
