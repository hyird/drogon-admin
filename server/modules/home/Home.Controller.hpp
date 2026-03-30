#pragma once

#include <drogon/HttpController.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
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
            // 查询各表的记录数（排除软删除的记录）
            auto userResult = co_await db_.execSqlCoro("SELECT COUNT(*) as count FROM sys_user WHERE deletedAt IS NULL");
            auto roleResult = co_await db_.execSqlCoro("SELECT COUNT(*) as count FROM sys_role WHERE deletedAt IS NULL");
            auto menuResult = co_await db_.execSqlCoro("SELECT COUNT(*) as count FROM sys_menu WHERE deletedAt IS NULL");
            auto deptResult = co_await db_.execSqlCoro("SELECT COUNT(*) as count FROM sys_department WHERE deletedAt IS NULL");

            HomeHelpers::HomeStatsSummary stats{
                .userCount = userResult[0]["count"].as<int>(),
                .roleCount = roleResult[0]["count"].as<int>(),
                .menuCount = menuResult[0]["count"].as<int>(),
                .departmentCount = deptResult[0]["count"].as<int>(),
            };

            co_return Response::ok(stats, HomeHelpers::homeStatsToJson);
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
};
