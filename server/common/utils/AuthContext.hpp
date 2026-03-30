#pragma once

#include <optional>

#include <drogon/HttpRequest.h>

#include "modules/system/SystemHelpers.hpp"

namespace AuthContext {

inline std::optional<SystemHelpers::AuthTokenClaims> tryGetAuthClaims(const drogon::HttpRequestPtr& req) {
    try {
        return req->attributes()->get<SystemHelpers::AuthTokenClaims>("authClaims");
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace AuthContext
