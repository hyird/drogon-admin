#pragma once

#include <exception>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include "common/cache/CacheManager.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/database/RedisService.hpp"
#include "modules/system/SystemHelpers.hpp"

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

        std::string menusSql = R"(
            SELECT id, name, parentId, type, path, component, permissionCode,
                   icon, status, `order`, 1 as visible
            FROM sys_menu
            WHERE status = 'enabled' AND deletedAt IS NULL
            ORDER BY `order` ASC, id ASC
        )";

        auto menusResult = co_await dbService.execSqlCoro(menusSql);

        std::vector<SystemHelpers::MenuSummary> menus;
        menus.reserve(menusResult.size());
        for (const auto& row : menusResult) {
            menus.push_back(SystemHelpers::menuSummaryFromRow(row));
        }

        co_await cacheManager.cacheAllMenus(menus);

        LOG_INFO << "Cache warmup completed: " << menus.size() << " menus loaded";
    } catch (const std::exception& e) {
        LOG_ERROR << "Cache warmup failed: " << e.what();
    }
}

}  // namespace StartupTasks
