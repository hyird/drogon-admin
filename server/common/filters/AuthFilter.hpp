#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/HttpAppFramework.h>
#include "common/utils/JwtUtils.hpp"
#include "common/utils/Response.hpp"
#include "common/utils/StringUtils.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/cache/CacheManager.hpp"
#include "modules/system/SystemHelpers.hpp"

using namespace drogon;

/**
 * @brief JWT 认证过滤器
 */
class AuthFilter : public HttpFilter<AuthFilter> {
private:
    std::shared_ptr<JwtUtils> jwtUtils_;
    CacheManager cacheManager_;
    DatabaseService dbService_;

public:
    AuthFilter() {
        auto config = app().getCustomConfig();
        std::string secret = config["jwt"]["secret"].asString();
        int expiresIn = config["jwt"]["access_token_expires_in"].asInt();
        jwtUtils_ = std::make_shared<JwtUtils>(secret, expiresIn);
    }

    void doFilter(const HttpRequestPtr& req,
                   FilterCallback&& fcb,
                   FilterChainCallback&& fccb) override {
        auto authHeader = req->getHeader("Authorization");

        if (authHeader.empty()) {
            fcb(Response::unauthorized("缺少认证令牌"));
            return;
        }

        if (!StringUtils::startsWith(authHeader, "Bearer ")) {
            fcb(Response::unauthorized("令牌格式错误"));
            return;
        }

        std::string token = authHeader.substr(7);

        // 使用协程检查 Token 黑名单
        drogon::async_run([this, token, req, fcb = std::move(fcb), fccb = std::move(fccb)]() mutable -> Task<void> {
            try {
                // 检查 Token 是否在黑名单中
                bool isBlacklisted = co_await cacheManager_.isTokenBlacklisted(token);
                if (isBlacklisted) {
                    LOG_INFO << "Token is blacklisted: " << token.substr(0, 20) << "...";
                    fcb(Response::unauthorized("令牌已失效，请重新登录"));
                    co_return;
                }

                // 验证 Token
                auto claims = jwtUtils_->verify(token, SystemHelpers::authTokenClaimsFromJson);
                if (!co_await isUserEnabled(claims.userId)) {
                    fcb(Response::unauthorized("用户已被禁用，请重新登录"));
                    co_return;
                }

                req->attributes()->insert("authClaims", claims);
                req->attributes()->insert("userId", claims.userId);

                fccb();

            } catch (const AppException& e) {
                fcb(Response::error(e.getCode(), e.getMessage(), e.getStatus()));
            } catch (const std::exception&) {
                fcb(Response::unauthorized("令牌验证失败"));
            }
        });
    }

private:
    Task<bool> isUserEnabled(int userId) {
        auto cachedSession = co_await cacheManager_.getUserSession(userId);
        if (cachedSession) {
            co_return cachedSession->status != "disabled";
        }

        auto cachedRecords = co_await cacheManager_.getUserRecords();
        if (cachedRecords) {
            for (const auto& record : *cachedRecords) {
                if (record.id == userId) {
                    co_return record.status != "disabled";
                }
            }

            co_return false;
        }

        auto result = co_await dbService_.execSqlCoro(
            "SELECT status FROM sys_user WHERE id = ? AND deletedAt IS NULL",
            {std::to_string(userId)});
        if (result.empty()) {
            co_return false;
        }

        co_return result[0]["status"].as<std::string>() != "disabled";
    }
};
