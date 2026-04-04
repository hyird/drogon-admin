#pragma once

#include <drogon/HttpController.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "common/cache/CacheManager.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/utils/Response.hpp"
#include "common/utils/AppException.hpp"
#include "HomeHelpers.hpp"

using namespace drogon;

/**
 * @brief 首页控制器
 */
class HomeController : public HttpController<HomeController> {
private:
    DatabaseService db_;
    CacheManager cache_;
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();

public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HomeController::stats, "/api/home/stats", Get, "AuthFilter");
    ADD_METHOD_TO(HomeController::systemInfo, "/api/home/system", Get, "AuthFilter");
    METHOD_LIST_END

    /**
     * @brief 获取统计数据
     */
    Task<HttpResponsePtr> stats(HttpRequestPtr /*req*/) {
        try {
            auto cached = co_await cache_.getHomeStats();
            if (cached) {
                co_return Response::ok(*cached, HomeHelpers::homeStatsToJson);
            }

            auto cachedStats = co_await loadHomeStatsFromRecordCaches();
            co_await cache_.cacheHomeStats(cachedStats);
            co_return Response::ok(cachedStats, HomeHelpers::homeStatsToJson);
        } catch (const std::exception& e) {
            LOG_ERROR << "HomeController::stats error: " << e.what();
            co_return Response::internalError("获取统计数据失败");
        }
    }

    /**
     * @brief 获取系统信息
     */
    Task<HttpResponsePtr> systemInfo(HttpRequestPtr /*req*/) {
        try {
            auto now = std::chrono::steady_clock::now();
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();

            // 获取当前时间
            auto currentTime = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(currentTime);
            std::ostringstream oss;
            std::tm tmBuf{};
#ifdef _WIN32
            gmtime_s(&tmBuf, &timeT);
#else
            gmtime_r(&timeT, &tmBuf);
#endif
            oss << std::put_time(&tmBuf, "%Y-%m-%dT%H:%M:%SZ");

            HomeHelpers::SystemInfoSummary info;
            info.version = "1.0.0";
            info.serverTime = oss.str();
            info.uptime = uptime;
#ifdef _WIN32
            info.platform = "Windows";
#elif __APPLE__
            info.platform = "macOS";
#else
            info.platform = "Linux";
#endif

            co_return Response::ok(info, HomeHelpers::systemInfoToJson);
        } catch (const std::exception& e) {
            LOG_ERROR << "HomeController::systemInfo error: " << e.what();
            co_return Response::internalError("获取系统信息失败");
        }
    }

private:
    Task<HomeHelpers::HomeStatsSummary> loadHomeStatsFromRecordCaches() {
        bool needUserCount = false;
        bool needRoleCount = false;
        bool needMenuCount = false;
        bool needDepartmentCount = false;

        auto users = co_await cache_.getUserRecords();
        HomeHelpers::HomeStatsSummary stats;
        if (users) {
            stats.userCount = static_cast<int>(users->size());
        } else {
            needUserCount = true;
        }

        auto roles = co_await cache_.getRoleRecords();
        if (roles) {
            stats.roleCount = static_cast<int>(roles->size());
        } else {
            needRoleCount = true;
        }

        auto menus = co_await cache_.getMenuRecords();
        if (menus) {
            stats.menuCount = static_cast<int>(menus->size());
        } else {
            needMenuCount = true;
        }

        auto departments = co_await cache_.getDepartmentRecords();
        if (departments) {
            stats.departmentCount = static_cast<int>(departments->size());
        } else {
            needDepartmentCount = true;
        }

        if (needUserCount || needRoleCount || needMenuCount || needDepartmentCount) {
            auto result = co_await db_.execSqlCoro(R"(
                SELECT
                    (SELECT COUNT(*) FROM sys_user WHERE deletedAt IS NULL) AS userCount,
                    (SELECT COUNT(*) FROM sys_role WHERE deletedAt IS NULL) AS roleCount,
                    (SELECT COUNT(*) FROM sys_menu WHERE deletedAt IS NULL) AS menuCount,
                    (SELECT COUNT(*) FROM sys_department WHERE deletedAt IS NULL) AS departmentCount
            )");
            if (!result.empty()) {
                if (needUserCount) {
                    stats.userCount = result[0]["userCount"].as<int>();
                }
                if (needRoleCount) {
                    stats.roleCount = result[0]["roleCount"].as<int>();
                }
                if (needMenuCount) {
                    stats.menuCount = result[0]["menuCount"].as<int>();
                }
                if (needDepartmentCount) {
                    stats.departmentCount = result[0]["departmentCount"].as<int>();
                }
            }
        }

        co_return stats;
    }
};
