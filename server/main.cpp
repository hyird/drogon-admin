#include <drogon/drogon.h>
#include <drogon/HttpAppFramework.h>
#include <iostream>

// Utils
#include "common/utils/PlatformUtils.hpp"
#include "common/utils/LoggerManager.hpp"
#include "common/utils/ConfigManager.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/ETagGenerator.hpp"
#include "common/utils/Response.hpp"
#include "common/utils/StartupTasks.hpp"

// Filters
#include "common/filters/AuthFilter.hpp"
#include "common/filters/PermissionFilter.hpp"

// Database
#include "common/database/DatabaseInitializer.hpp"
#include "common/utils/AuthContext.hpp"

// Controllers - System Module
#include "modules/system/Auth.Controller.hpp"
#include "modules/system/User.Controller.hpp"
#include "modules/system/Role.Controller.hpp"
#include "modules/system/Menu.Controller.hpp"
#include "modules/system/Department.Controller.hpp"

// Controllers - Home Module
#include "modules/home/Home.Controller.hpp"

using namespace drogon;

/**
 * @brief 设置全局异常处理器
 */
void setupExceptionHandler() {
    app().setExceptionHandler([](const std::exception& e,
                                   const HttpRequestPtr& /*req*/,
                                   std::function<void (const HttpResponsePtr &)> &&callback) {
        if (const auto* appEx = dynamic_cast<const AppException*>(&e)) {
            callback(Response::error(appEx->getCode(), appEx->getMessage(), appEx->getStatus()));
        } else {
            LOG_ERROR << "Unhandled exception: " << e.what();
            callback(Response::internalError("服务器内部错误"));
        }
    });
}

/**
 * @brief 注册请求/响应拦截器
 */
void setupAdvices() {
    // 请求前拦截：记录请求开始时间和请求体
    app().registerPreHandlingAdvice([](const HttpRequestPtr &req) {
        req->attributes()->insert("startTime", std::chrono::steady_clock::now());

        if (req->method() != Get && !req->body().empty()) {
            std::string body = std::string(req->body());
            if (body.length() > 1000) {
                body = body.substr(0, 1000) + "...(truncated)";
            }
            req->attributes()->insert("requestBody", body);
        }
    });

    // 请求后拦截：处理ETag和记录日志
    app().registerPostHandlingAdvice([](const HttpRequestPtr &req, const HttpResponsePtr &resp) {
        // ETag 处理
        if (req->method() == Get && resp->statusCode() == k200OK && !resp->body().empty()) {
            std::string etag = ETagGenerator::generate(std::string(resp->body()));
            resp->addHeader("ETag", etag);

            std::string ifNoneMatch = req->getHeader("If-None-Match");
            if (!ifNoneMatch.empty() && ifNoneMatch == etag) {
                resp->setStatusCode(k304NotModified);
                resp->setBody("");
            }
        }

        // 记录请求日志
        std::string username = "-";
        if (auto claims = AuthContext::tryGetAuthClaims(req); claims) {
            username = claims->username;
        }

        std::string duration = "-";
        try {
            auto startTime = req->attributes()->get<std::chrono::steady_clock::time_point>("startTime");
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            duration = std::to_string(elapsed) + "ms";
        } catch (...) {}

        std::string body;
        try {
            body = req->attributes()->get<std::string>("requestBody");
        } catch (...) {}

        LOG_DEBUG << "[" << username << "] "
                  << req->methodString() << " " << req->path()
                  << (req->query().empty() ? "" : "?" + req->query())
                  << " -> " << static_cast<int>(resp->statusCode())
                  << " (" << duration << ")"
                  << (body.empty() ? "" : " " + body);
    });
}

/**
 * @brief 服务器启动回调
 */
void onServerStarted() {
    auto listeners = app().getListeners();
    std::cout << "Drogon Admin Server started" << std::endl;
    for (const auto& addr : listeners) {
        std::cout << "  -> http://" << addr.toIpPort() << std::endl;
        LOG_INFO << "Server listening on http://" << addr.toIpPort();
    }
    std::cout << "Logs: ./logs/server.log" << std::endl;

    // 先完成数据库初始化，再执行缓存预热，避免读取未建表的数据
    async_run([]() -> Task<> {
        LOG_INFO << "Starting database initialization...";
        const bool databaseReady = co_await DatabaseInitializer::initialize();
        if (!databaseReady) {
            LOG_ERROR << "Database initialization failed, skipping cache warmup";
            co_return;
        }
        LOG_INFO << "Starting cache warmup...";
        co_await StartupTasks::warmupCache();
        LOG_INFO << "Startup database and cache tasks completed";
    });
}

int main() {
    // 1. 平台特定初始化
    PlatformUtils::initialize();

    // 2. 初始化日志系统
    LoggerManager::initialize("./logs/server.log");

    // 3. 加载配置文件
    if (!ConfigManager::load()) {
        LOG_FATAL << "Failed to load config file";
        return 1;
    }

    // 4. 应用配置
    LoggerManager::setLogLevel(ConfigManager::getLogLevel());
    LoggerManager::setConsoleOutput(ConfigManager::isConsoleLogEnabled());

    // 5. 设置全局异常处理
    setupExceptionHandler();

    // 6. 注册请求/响应拦截器
    setupAdvices();

    // 7. 注册启动回调
    app().registerBeginningAdvice([]() {
        // 检查 Redis 是否可用
        bool redisAvailable = StartupTasks::checkRedisAvailable();
        if (!redisAvailable) {
            LOG_WARN << "Redis is not available. Cache features will be disabled.";
            AppRedisConfig::enabled() = false;
        }

        // 调用原始的启动回调
        onServerStarted();
    });

    // 8. SPA fallback: 非 API 路径且非静态文件返回 index.html
    app().setCustom404Page(HttpResponse::newFileResponse("./web/index.html"));

    // 9. 启动服务器
    app().run();

    return 0;
}
