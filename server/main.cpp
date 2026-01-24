#include <drogon/drogon.h>
#include <drogon/HttpAppFramework.h>
#include <iostream>

// Utils
#include "common/utils/PlatformUtils.hpp"
#include "common/utils/LoggerManager.hpp"
#include "common/utils/ConfigManager.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/ETagGenerator.hpp"

// Filters
#include "common/filters/AuthFilter.hpp"
#include "common/filters/PermissionFilter.hpp"

// Database
#include "common/database/DatabaseInitializer.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/database/RedisService.hpp"
#include "common/cache/CacheManager.hpp"

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
        Json::Value json;
        HttpStatusCode status = k500InternalServerError;

        if (const auto* appEx = dynamic_cast<const AppException*>(&e)) {
            json["code"] = appEx->getCode();
            json["message"] = appEx->getMessage();
            status = appEx->getStatus();
        } else {
            LOG_ERROR << "Unhandled exception: " << e.what();
            json["code"] = "INTERNAL_ERROR";
            json["message"] = "服务器内部错误";
        }

        json["status"] = static_cast<int>(status);
        auto resp = HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(status);
        callback(resp);
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
        try {
            username = req->attributes()->get<std::string>("username");
        } catch (...) {}

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
 * @brief 缓存预热（预加载热点数据）
 */
Task<void> warmupCache() {
    // 如果 Redis 未启用，跳过缓存预热
    if (!AppRedisConfig::enabled()) {
        LOG_INFO << "Cache is disabled, skipping warmup";
        co_return;
    }

    try {
        DatabaseService dbService;
        CacheManager cacheManager;

        // 预加载全局菜单
        std::string menusSql = R"(
            SELECT id, name, parentId, type, path, component, permissionCode,
                   icon, status, `order`, 1 as visible
            FROM sys_menu
            WHERE status = 'enabled' AND deletedAt IS NULL
            ORDER BY `order` ASC, id ASC
        )";

        auto menusResult = co_await dbService.execSqlCoro(menusSql);

        Json::Value menus(Json::arrayValue);
        for (const auto& row : menusResult) {
            Json::Value menu;
            menu["id"] = row["id"].as<int>();
            menu["name"] = row["name"].as<std::string>();
            menu["parentId"] = row["parentId"].as<int>();
            menu["type"] = row["type"].as<std::string>();
            menu["path"] = row["path"].isNull() ? "" : row["path"].as<std::string>();
            menu["component"] = row["component"].isNull() ? "" : row["component"].as<std::string>();
            menu["permissionCode"] = row["permissionCode"].isNull() ? "" : row["permissionCode"].as<std::string>();
            menu["icon"] = row["icon"].isNull() ? "" : row["icon"].as<std::string>();
            menu["order"] = row["order"].as<int>();
            menu["visible"] = row["visible"].as<int>() == 1;
            menus.append(menu);
        }

        // 缓存全局菜单
        co_await cacheManager.cacheAllMenus(menus);

        LOG_INFO << "Cache warmup completed: " << menus.size() << " menus loaded";

    } catch (const std::exception& e) {
        LOG_ERROR << "Cache warmup failed: " << e.what();
    }
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

    // 初始化数据库
    async_run([]() -> Task<> {
        co_await DatabaseInitializer::initialize();
    });

    // 缓存预热
    async_run([]() -> Task<> {
        co_await warmupCache();
    });
}

/**
 * @brief 检查 Redis 是否可用
 */
bool checkRedisAvailable() {
    // 先尝试 fast Redis 客户端（配置文件中设置了 is_fast: true）
    try {
        auto client = app().getFastRedisClient("default");
        if (client) {
            AppRedisConfig::useFast() = true;
            LOG_INFO << "Redis is available (fast mode: true)";
            return true;
        }
    } catch (const std::exception&) {
        // Fast Redis 不可用，尝试普通 Redis
    }

    // 尝试普通 Redis 客户端
    try {
        auto client = app().getRedisClient("default");
        if (client) {
            AppRedisConfig::useFast() = false;
            LOG_INFO << "Redis is available (fast mode: false)";
            return true;
        }
    } catch (const std::exception& e) {
        LOG_WARN << "Redis is not available: " << e.what();
    }

    return false;
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
        bool redisAvailable = checkRedisAvailable();
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
