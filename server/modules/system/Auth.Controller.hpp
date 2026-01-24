#pragma once

#include <drogon/HttpController.h>
#include "common/utils/Response.hpp"
#include "common/utils/JwtUtils.hpp"
#include "common/utils/PasswordUtils.hpp"
#include "common/utils/AppException.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/FieldHelper.hpp"

using namespace drogon;

/**
 * @brief 认证控制器
 */
class AuthController : public HttpController<AuthController> {
private:
    std::shared_ptr<JwtUtils> jwtUtils_;
    std::shared_ptr<JwtUtils> refreshJwtUtils_;
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::login, "/api/auth/login", Post);
    ADD_METHOD_TO(AuthController::refresh, "/api/auth/refresh", Post);
    ADD_METHOD_TO(AuthController::logout, "/api/auth/logout", Post, "AuthFilter");
    ADD_METHOD_TO(AuthController::getCurrentUser, "/api/auth/me", Get, "AuthFilter");
    METHOD_LIST_END

    AuthController() {
        auto config = app().getCustomConfig();

        std::string secret = config["jwt"]["secret"].asString();
        int accessExpiresIn = config["jwt"]["access_token_expires_in"].asInt();
        jwtUtils_ = std::make_shared<JwtUtils>(secret, accessExpiresIn);

        std::string refreshSecret = config["jwt"]["refresh_token_secret"].asString();
        int refreshExpiresIn = config["jwt"]["refresh_token_expires_in"].asInt();
        refreshJwtUtils_ = std::make_shared<JwtUtils>(refreshSecret, refreshExpiresIn);
    }

    Task<HttpResponsePtr> login(HttpRequestPtr req) {
        try {
            auto json = req->getJsonObject();
            if (!json) {
                co_return Response::badRequest("请求体格式错误");
            }

            std::string username = (*json)["username"].asString();
            std::string password = (*json)["password"].asString();

            if (username.empty() || password.empty()) {
                co_return Response::badRequest("用户名和密码不能为空");
            }

            // 检查登录失败次数
            auto failureCount = co_await cacheManager_.getLoginFailureCount(username);
            if (failureCount >= 5) {
                co_return Response::error(429, "登录失败次数过多，请15分钟后再试", k429TooManyRequests);
            }

            std::string sql = R"(
                SELECT id, username, passwordHash, nickname, status
                FROM sys_user
                WHERE username = ? AND deletedAt IS NULL
            )";

            auto result = co_await dbService_.execSqlCoro(sql, {username});

            if (result.empty()) {
                co_await cacheManager_.recordLoginFailure(username);
                throw AuthException::PasswordIncorrect();
            }

            auto row = result[0];
            int userId = F_INT(row["id"]);
            std::string passwordHash = F_STR(row["passwordHash"]);
            std::string nickname = F_STR_DEF(row["nickname"], "");
            std::string status = F_STR(row["status"]);

            if (!PasswordUtils::verifyPassword(password, passwordHash)) {
                co_await cacheManager_.recordLoginFailure(username);
                throw AuthException::PasswordIncorrect();
            }

            if (status == "disabled") {
                throw AuthException::UserDisabled();
            }

            // 登录成功，清除失败记录
            co_await cacheManager_.clearLoginFailure(username);

            Json::Value tokenPayload;
            tokenPayload["userId"] = userId;
            tokenPayload["username"] = username;

            std::string accessToken = jwtUtils_->sign(tokenPayload);
            std::string refreshToken = refreshJwtUtils_->sign(tokenPayload);

            auto userInfo = co_await buildUserInfo(userId, username, nickname, status);

            // 缓存用户会话信息
            co_await cacheManager_.cacheUserSession(userId, userInfo);

            Json::Value data;
            data["token"] = accessToken;
            data["refreshToken"] = refreshToken;
            data["user"] = userInfo;

            co_return Response::ok(data, "登录成功");

        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "Login error: " << e.what();
            co_return Response::internalError("登录失败");
        }
    }

    Task<HttpResponsePtr> refresh(HttpRequestPtr req) {
        try {
            auto json = req->getJsonObject();
            if (!json) {
                co_return Response::badRequest("请求体格式错误");
            }

            std::string refreshToken = (*json)["refreshToken"].asString();
            if (refreshToken.empty()) {
                co_return Response::badRequest("刷新令牌不能为空");
            }

            Json::Value payload = refreshJwtUtils_->verify(refreshToken);
            int userId = payload["userId"].asInt();
            std::string username = payload["username"].asString();

            std::string sql = R"(
                SELECT id, status
                FROM sys_user
                WHERE id = ? AND deletedAt IS NULL
            )";

            auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(userId)});

            if (result.empty()) {
                throw AuthException::UserNotFound();
            }

            std::string status = F_STR(result[0]["status"]);
            if (status == "disabled") {
                throw AuthException::UserDisabled();
            }

            Json::Value tokenPayload;
            tokenPayload["userId"] = userId;
            tokenPayload["username"] = username;

            std::string newAccessToken = jwtUtils_->sign(tokenPayload);
            std::string newRefreshToken = refreshJwtUtils_->sign(tokenPayload);

            Json::Value data;
            data["token"] = newAccessToken;
            data["refreshToken"] = newRefreshToken;

            co_return Response::ok(data, "刷新成功");

        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "Refresh token error: " << e.what();
            co_return Response::internalError("刷新令牌失败");
        }
    }

    Task<HttpResponsePtr> logout(HttpRequestPtr req) {
        try {
            auto attrs = req->attributes();
            int userId = attrs->get<int>("userId");

            // 从 Header 中提取 Token
            std::string authorization = req->getHeader("Authorization");
            std::string token;
            if (authorization.find("Bearer ") == 0) {
                token = authorization.substr(7);
            } else {
                co_return Response::badRequest("Token 格式错误");
            }

            // 验证 Token 并获取剩余有效期
            try {
                Json::Value payload = jwtUtils_->verify(token);
                int exp = payload["exp"].asInt();
                int now = static_cast<int>(std::time(nullptr));
                int remainingTtl = exp - now;

                if (remainingTtl > 0) {
                    // 将 Token 加入黑名单（TTL 为剩余有效期）
                    co_await cacheManager_.blacklistToken(token, remainingTtl);
                    LOG_INFO << "Token blacklisted for userId: " << userId << ", TTL: " << remainingTtl;
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Failed to verify token during logout: " << e.what();
                // Token 已失效，忽略
            }

            // 清除用户会话缓存
            co_await cacheManager_.deleteUserSession(userId);

            co_return Response::ok("登出成功");

        } catch (const std::exception& e) {
            LOG_ERROR << "Logout error: " << e.what();
            co_return Response::internalError("登出失败");
        }
    }

    Task<HttpResponsePtr> getCurrentUser(HttpRequestPtr req) {
        try {
            auto attrs = req->attributes();
            if (!attrs->find("userId") || !attrs->find("username")) {
                co_return Response::unauthorized("未授权访问");
            }

            int userId = attrs->get<int>("userId");
            std::string username = attrs->get<std::string>("username");

            // 先尝试从缓存获取
            auto cached = co_await cacheManager_.getUserSession(userId);
            if (cached) {
                LOG_DEBUG << "User session cache hit for userId: " << userId;
                co_return Response::ok(*cached, "获取成功");
            }

            LOG_DEBUG << "User session cache miss for userId: " << userId;

            std::string sql = R"(
                SELECT id, username, nickname, status
                FROM sys_user
                WHERE id = ? AND deletedAt IS NULL
            )";

            auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(userId)});

            if (result.empty()) {
                throw AuthException::UserNotFound();
            }

            auto row = result[0];
            std::string nickname = F_STR_DEF(row["nickname"], "");
            std::string status = F_STR(row["status"]);

            if (status == "disabled") {
                throw AuthException::UserDisabled();
            }

            auto userInfo = co_await buildUserInfo(userId, username, nickname, status);

            // 缓存结果
            co_await cacheManager_.cacheUserSession(userId, userInfo);

            co_return Response::ok(userInfo, "获取成功");

        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "Get current user error: " << e.what();
            co_return Response::internalError("获取用户信息失败");
        }
    }

private:
    Task<Json::Value> buildUserInfo(int userId, const std::string& username,
                                      const std::string& nickname, const std::string& status) {
        Json::Value userInfo;
        userInfo["id"] = userId;
        userInfo["username"] = username;
        userInfo["nickname"] = nickname;
        userInfo["status"] = status;

        auto roles = co_await getUserRoles(userId);
        userInfo["roles"] = roles;

        bool isSuperadmin = false;
        for (const auto& role : roles) {
            if (role["code"].asString() == "superadmin") {
                isSuperadmin = true;
                break;
            }
        }

        Json::Value menus;
        if (isSuperadmin) {
            menus = co_await getAllMenus();
        } else {
            menus = co_await getUserMenus(userId);
        }
        userInfo["menus"] = menus;

        co_return userInfo;
    }

    Task<Json::Value> getUserRoles(int userId) {
        // 先尝试从缓存获取
        auto cached = co_await cacheManager_.getUserRoles(userId);
        if (cached) {
            LOG_DEBUG << "User roles cache hit for userId: " << userId;
            co_return *cached;
        }

        LOG_DEBUG << "User roles cache miss for userId: " << userId;

        std::string sql = R"(
            SELECT r.id, r.name, r.code
            FROM sys_role r
            INNER JOIN sys_user_role ur ON r.id = ur.roleId
            WHERE ur.userId = ? AND r.deletedAt IS NULL
        )";

        auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(userId)});

        Json::Value roles(Json::arrayValue);
        for (const auto& row : result) {
            Json::Value role;
            role["id"] = F_INT(row["id"]);
            role["name"] = F_STR(row["name"]);
            role["code"] = F_STR(row["code"]);
            roles.append(role);
        }

        // 缓存结果
        co_await cacheManager_.cacheUserRoles(userId, roles);

        co_return roles;
    }

    Task<Json::Value> getAllMenus() {
        // 先尝试从缓存获取
        auto cached = co_await cacheManager_.getAllMenus();
        if (cached) {
            LOG_DEBUG << "All menus cache hit";
            co_return *cached;
        }

        LOG_DEBUG << "All menus cache miss";

        std::string sql = R"(
            SELECT id, name, parentId, type, path, component, permissionCode,
                   icon, status, `order`, 1 as visible
            FROM sys_menu
            WHERE status = 'enabled' AND deletedAt IS NULL
            ORDER BY `order` ASC, id ASC
        )";

        auto result = co_await dbService_.execSqlCoro(sql);

        Json::Value menus(Json::arrayValue);
        for (const auto& row : result) {
            Json::Value menu;
            menu["id"] = F_INT(row["id"]);
            menu["name"] = F_STR(row["name"]);
            menu["parentId"] = F_INT(row["parentId"]);
            menu["type"] = F_STR(row["type"]);
            menu["path"] = F_STR_DEF(row["path"], "");
            menu["component"] = F_STR_DEF(row["component"], "");
            menu["permissionCode"] = F_STR_DEF(row["permissionCode"], "");
            menu["icon"] = F_STR_DEF(row["icon"], "");
            menu["order"] = F_INT(row["order"]);
            menu["visible"] = F_BOOL(row["visible"]);
            menus.append(menu);
        }

        // 缓存结果
        co_await cacheManager_.cacheAllMenus(menus);

        co_return menus;
    }

    Task<Json::Value> getUserMenus(int userId) {
        // 先尝试从缓存获取
        auto cached = co_await cacheManager_.getUserMenus(userId);
        if (cached) {
            LOG_DEBUG << "User menus cache hit for userId: " << userId;
            co_return *cached;
        }

        LOG_DEBUG << "User menus cache miss for userId: " << userId;

        std::string sql = R"(
            SELECT DISTINCT m.id, m.name, m.parentId, m.type, m.path, m.component,
                   m.permissionCode, m.icon, m.status, m.`order`, 1 as visible
            FROM sys_menu m
            INNER JOIN sys_role_menu rm ON m.id = rm.menuId
            INNER JOIN sys_user_role ur ON rm.roleId = ur.roleId
            INNER JOIN sys_role r ON ur.roleId = r.id
            WHERE ur.userId = ?
              AND r.status = 'enabled' AND r.deletedAt IS NULL
              AND m.status = 'enabled' AND m.deletedAt IS NULL
            ORDER BY m.`order` ASC, m.id ASC
        )";

        auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(userId)});

        Json::Value menus(Json::arrayValue);
        for (const auto& row : result) {
            Json::Value menu;
            menu["id"] = F_INT(row["id"]);
            menu["name"] = F_STR(row["name"]);
            menu["parentId"] = F_INT(row["parentId"]);
            menu["type"] = F_STR(row["type"]);
            menu["path"] = F_STR_DEF(row["path"], "");
            menu["component"] = F_STR_DEF(row["component"], "");
            menu["permissionCode"] = F_STR_DEF(row["permissionCode"], "");
            menu["icon"] = F_STR_DEF(row["icon"], "");
            menu["order"] = F_INT(row["order"]);
            menu["visible"] = F_BOOL(row["visible"]);
            menus.append(menu);
        }

        // 缓存结果
        co_await cacheManager_.cacheUserMenus(userId, menus);

        co_return menus;
    }
};
