#pragma once

#include <ctime>

#include <drogon/HttpController.h>
#include "common/utils/Response.hpp"
#include "common/utils/ConfigManager.hpp"
#include "common/utils/RequestValidation.hpp"
#include "common/utils/JwtUtils.hpp"
#include "common/utils/PasswordUtils.hpp"
#include "common/utils/AppException.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/AuthContext.hpp"
#include "SystemConstants.hpp"
#include "SystemHelpers.hpp"
#include "AuthSessionBuilder.hpp"
#include "AuthRecordLoader.hpp"
#include "AuthRequests.hpp"

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
            auto request = AuthRequests::makeLoginRequest(req);
            const std::string clientIp = req->getPeerAddr().toIp();
            const int loginRateLimitMaxRequests = ConfigManager::getLoginRateLimitMaxRequests();
            const int loginRateLimitWindowSeconds = ConfigManager::getLoginRateLimitWindowSeconds();

            std::string rateLimitScope = clientIp + ":" + request.username;
            auto loginAttempts = co_await cacheManager_.checkRateLimitKey(
                rateLimitScope, loginRateLimitMaxRequests, loginRateLimitWindowSeconds);
            if (loginAttempts > loginRateLimitMaxRequests) {
                co_return Response::error(429, "登录请求过于频繁，请稍后再试", k429TooManyRequests);
            }

            // 检查登录失败次数
            auto failureCount = co_await cacheManager_.getLoginFailureCount(request.username);
            if (failureCount >= 5) {
                co_return Response::error(429, "登录失败次数过多，请15分钟后再试", k429TooManyRequests);
            }

            auto userRecord = co_await AuthRecordLoader::loadUserRecordByUsername(dbService_, cacheManager_, request.username);
            if (!userRecord) {
                co_await cacheManager_.recordLoginFailure(request.username);
                throw AuthException::PasswordIncorrect();
            }

            auto passwordHash = co_await AuthRecordLoader::loadUserPasswordHash(dbService_, userRecord->id);
            if (!passwordHash || !PasswordUtils::verifyPassword(request.password, *passwordHash)) {
                co_await cacheManager_.recordLoginFailure(request.username);
                throw AuthException::PasswordIncorrect();
            }

            if (userRecord->status == "disabled") {
                throw AuthException::UserDisabled();
            }

            // 登录成功，清除失败记录
            co_await cacheManager_.clearLoginFailure(request.username);

            SystemHelpers::AuthTokenClaims tokenClaims{
                .userId = userRecord->id,
                .username = userRecord->username,
            };

            std::string accessToken = jwtUtils_->sign(tokenClaims, SystemHelpers::authTokenClaimsToJson);
            std::string refreshToken = refreshJwtUtils_->sign(tokenClaims, SystemHelpers::authTokenClaimsToJson);

            auto userInfo = co_await AuthSessionBuilder::buildUserInfo(
                dbService_,
                cacheManager_,
                userRecord->id,
                userRecord->username,
                userRecord->nickname,
                userRecord->status);

            // 缓存用户会话信息
            co_await cacheManager_.cacheUserSession(userRecord->id, userInfo);
            SystemHelpers::LoginResponseSummary response{
                .tokens = {
                    .token = accessToken,
                    .refreshToken = refreshToken,
                },
                .user = std::move(userInfo),
            };

            co_return Response::ok(response, SystemHelpers::loginResponseToJson, "登录成功");

        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "Login error: " << e.what();
            co_return Response::internalError("登录失败");
        }
    }

    Task<HttpResponsePtr> refresh(HttpRequestPtr req) {
        try {
            auto request = AuthRequests::makeRefreshRequest(req);

            auto tokenClaims = refreshJwtUtils_->verify(request.refreshToken, SystemHelpers::authTokenClaimsFromJson);
            int userId = tokenClaims.userId;
            auto userRecord = co_await AuthRecordLoader::loadUserRecordById(dbService_, cacheManager_, userId);
            if (!userRecord) {
                throw AuthException::UserNotFound();
            }

            if (userRecord->status == "disabled") {
                throw AuthException::UserDisabled();
            }

            SystemHelpers::AuthTokenClaims claims{
                .userId = userId,
                .username = userRecord->username.empty() ? tokenClaims.username : userRecord->username,
            };

            SystemHelpers::AuthTokensSummary tokens{
                .token = jwtUtils_->sign(claims, SystemHelpers::authTokenClaimsToJson),
                .refreshToken = refreshJwtUtils_->sign(claims, SystemHelpers::authTokenClaimsToJson),
            };

            co_return Response::ok(tokens, SystemHelpers::authTokensToJson, "刷新成功");

        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "Refresh token error: " << e.what();
            co_return Response::internalError("刷新令牌失败");
        }
    }

    Task<HttpResponsePtr> logout(HttpRequestPtr req) {
        try {
            SystemHelpers::AuthTokenClaims claims;
            if (auto authClaims = AuthContext::tryGetAuthClaims(req); authClaims) {
                claims = *authClaims;
            } else {
                claims.userId = req->attributes()->get<int>("userId");
            }

            std::string token = RequestValidation::requireBearerToken(req);

            // 验证 Token 并获取剩余有效期
            try {
                auto tokenClaims = jwtUtils_->verify(token, SystemHelpers::authTokenClaimsFromJson);
                int exp = static_cast<int>(tokenClaims.expiresAt);
                int now = static_cast<int>(std::time(nullptr));
                int remainingTtl = exp - now;

                if (remainingTtl > 0) {
                    // 将 Token 加入黑名单（TTL 为剩余有效期）
                    co_await cacheManager_.blacklistToken(token, remainingTtl);
                    LOG_INFO << "Token blacklisted for userId: " << claims.userId << ", TTL: " << remainingTtl;
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Failed to verify token during logout: " << e.what();
                // Token 已失效，忽略
            }

            // 使当前用户相关缓存失效，避免退出后仍命中旧会话快照
            co_await cacheManager_.clearUserCache(claims.userId);

            co_return Response::success("登出成功");

        } catch (const std::exception& e) {
            LOG_ERROR << "Logout error: " << e.what();
            co_return Response::internalError("登出失败");
        }
    }

    Task<HttpResponsePtr> getCurrentUser(HttpRequestPtr req) {
        try {
            auto attrs = req->attributes();
            if (!attrs->find("authClaims") && !attrs->find("userId")) {
                co_return Response::unauthorized("未授权访问");
            }

            SystemHelpers::AuthTokenClaims claims;
            if (auto authClaims = AuthContext::tryGetAuthClaims(req); authClaims) {
                claims = *authClaims;
            } else {
                claims.userId = attrs->get<int>("userId");
            }

            // 先尝试从缓存获取
            auto cached = co_await cacheManager_.getUserSession(claims.userId);
            if (cached) {
                LOG_DEBUG << "User session cache hit for userId: " << claims.userId;
                co_return Response::ok(*cached, SystemHelpers::currentUserToJson, "获取成功");
            }

            LOG_DEBUG << "User session cache miss for userId: " << claims.userId;

            auto userRecord = co_await AuthRecordLoader::loadUserRecordById(dbService_, cacheManager_, claims.userId);
            if (!userRecord) {
                throw AuthException::UserNotFound();
            }

            if (userRecord->status == "disabled") {
                throw AuthException::UserDisabled();
            }

            claims.username = userRecord->username.empty() ? claims.username : userRecord->username;

            auto userInfo = co_await AuthSessionBuilder::buildUserInfo(
                dbService_,
                cacheManager_,
                userRecord->id,
                claims.username,
                userRecord->nickname,
                userRecord->status);
            // 缓存结果
            co_await cacheManager_.cacheUserSession(userRecord->id, userInfo);

            co_return Response::ok(userInfo, SystemHelpers::currentUserToJson, "获取成功");

        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "Get current user error: " << e.what();
            co_return Response::internalError("获取用户信息失败");
        }
    }

private:
};
