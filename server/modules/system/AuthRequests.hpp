#pragma once

#include <string>

#include <json/json.h>

#include "common/utils/RequestValidation.hpp"

namespace AuthRequests {

struct LoginRequest {
    std::string username;
    std::string password;
};

struct RefreshRequest {
    std::string refreshToken;
};

inline LoginRequest makeLoginRequest(const Json::Value& json) {
    LoginRequest request;
    request.username = RequestValidation::requireStringField(json, "username", "用户名", 1, 50);
    request.password = RequestValidation::requireStringField(json, "password", "密码", 1, 100);
    return request;
}

inline LoginRequest makeLoginRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeLoginRequest(body);
    });
}

inline RefreshRequest makeRefreshRequest(const Json::Value& json) {
    RefreshRequest request;
    request.refreshToken = RequestValidation::requireStringField(json, "refreshToken", "刷新令牌", 1, 2048);
    return request;
}

inline RefreshRequest makeRefreshRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeRefreshRequest(body);
    });
}

}  // namespace AuthRequests
