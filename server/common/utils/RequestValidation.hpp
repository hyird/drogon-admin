#pragma once

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "AppException.hpp"
#include "StringUtils.hpp"

namespace RequestValidation {

inline const std::regex PHONE_REGEX(R"(^1[3-9]\d{9}$)");
inline const std::regex EMAIL_REGEX(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
inline const std::regex ROLE_CODE_REGEX(R"(^[a-zA-Z][a-zA-Z0-9_]*$)");

inline Json::Value requireJsonBody(const drogon::HttpRequestPtr& req) {
    auto json = req->getJsonObject();
    if (!json) {
        throw ValidationException("请求体格式错误");
    }
    return *json;
}

template <typename Builder>
inline auto makeJsonBodyRequest(const drogon::HttpRequestPtr& req, Builder&& builder) {
    return std::forward<Builder>(builder)(requireJsonBody(req));
}

inline bool matchesAllowed(const std::string& value,
                           std::initializer_list<std::string_view> allowed) {
    return std::find_if(allowed.begin(), allowed.end(),
                        [value](std::string_view candidate) { return candidate == value; }) !=
           allowed.end();
}

inline void requirePattern(const std::string& value, const std::regex& pattern,
                           const std::string& message) {
    if (!std::regex_match(value, pattern)) {
        throw ValidationException(message);
    }
}

inline int parsePositiveIntText(const std::string& text, const std::string& label) {
    auto trimmed = StringUtils::trim(text);
    if (trimmed.empty()) {
        throw ValidationException(label + "不能为空");
    }

    long long parsed = 0;
    auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), parsed);
    if (ec != std::errc() || ptr != trimmed.data() + trimmed.size()) {
        throw ValidationException(label + "必须是正整数");
    }

    if (parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
        throw ValidationException(label + "必须是正整数");
    }

    return static_cast<int>(parsed);
}

inline int parseNonNegativeIntText(const std::string& text, const std::string& label) {
    auto trimmed = StringUtils::trim(text);
    if (trimmed.empty()) {
        throw ValidationException(label + "不能为空");
    }

    long long parsed = 0;
    auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), parsed);
    if (ec != std::errc() || ptr != trimmed.data() + trimmed.size()) {
        throw ValidationException(label + "必须是非负整数");
    }

    if (parsed < 0 || parsed > std::numeric_limits<int>::max()) {
        throw ValidationException(label + "必须是非负整数");
    }

    return static_cast<int>(parsed);
}

inline bool hasAnyField(const Json::Value& body,
                        std::initializer_list<std::string_view> fields) {
    for (std::string_view field : fields) {
        if (body.isMember(std::string(field))) {
            return true;
        }
    }
    return false;
}

inline void requireAnyField(const Json::Value& body,
                            std::initializer_list<std::string_view> fields,
                            const std::string& message = "请求参数不能为空") {
    if (!hasAnyField(body, fields)) {
        throw ValidationException(message);
    }
}

inline int parsePositiveIntValue(const Json::Value& value, const std::string& label) {
    auto fail = [&label]() {
        throw ValidationException(label + "必须是正整数");
    };

    long long parsed = 0;
    if (value.isInt()) {
        parsed = static_cast<long long>(value.asInt());
    } else if (value.isUInt()) {
        parsed = static_cast<long long>(value.asUInt());
    } else if (value.isInt64()) {
        parsed = static_cast<long long>(value.asInt64());
    } else if (value.isUInt64()) {
        auto raw = value.asUInt64();
        if (raw > static_cast<Json::UInt64>(std::numeric_limits<int>::max())) {
            fail();
        }
        parsed = static_cast<long long>(raw);
    } else if (value.isString()) {
        return parsePositiveIntText(value.asString(), label);
    } else {
        fail();
    }

    if (parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
        fail();
    }

    return static_cast<int>(parsed);
}

inline int parseNonNegativeIntValue(const Json::Value& value, const std::string& label) {
    auto fail = [&label]() {
        throw ValidationException(label + "必须是非负整数");
    };

    long long parsed = 0;
    if (value.isInt()) {
        parsed = static_cast<long long>(value.asInt());
    } else if (value.isUInt()) {
        parsed = static_cast<long long>(value.asUInt());
    } else if (value.isInt64()) {
        parsed = static_cast<long long>(value.asInt64());
    } else if (value.isUInt64()) {
        auto raw = value.asUInt64();
        if (raw > static_cast<Json::UInt64>(std::numeric_limits<int>::max())) {
            fail();
        }
        parsed = static_cast<long long>(raw);
    } else if (value.isString()) {
        return parseNonNegativeIntText(value.asString(), label);
    } else {
        fail();
    }

    if (parsed < 0 || parsed > std::numeric_limits<int>::max()) {
        fail();
    }

    return static_cast<int>(parsed);
}

inline std::optional<std::string> optionalStringField(const Json::Value& body,
                                                      const std::string& field,
                                                      const std::string& label,
                                                      size_t minLength = 0,
                                                      size_t maxLength = 0) {
    if (!body.isMember(field)) {
        return std::nullopt;
    }

    if (body[field].isNull()) {
        throw ValidationException(label + "不能为空");
    }

    if (!body[field].isString()) {
        throw ValidationException(label + "格式错误");
    }

    std::string value = StringUtils::trim(body[field].asString());
    if (value.empty()) {
        throw ValidationException(label + "不能为空");
    }

    if (minLength > 0 && value.size() < minLength) {
        throw ValidationException(label + "长度不能少于" + std::to_string(minLength) + "位");
    }

    if (maxLength > 0 && value.size() > maxLength) {
        throw ValidationException(label + "长度不能超过" + std::to_string(maxLength) + "位");
    }

    return value;
}

inline std::string requireStringField(const Json::Value& body, const std::string& field,
                                      const std::string& label, size_t minLength = 1,
                                      size_t maxLength = 0) {
    auto value = optionalStringField(body, field, label, minLength, maxLength);
    if (!value) {
        throw ValidationException("请输入" + label);
    }
    return *value;
}

inline std::optional<std::string> optionalEnumField(const Json::Value& body,
                                                    const std::string& field,
                                                    const std::string& label,
                                                    std::initializer_list<std::string_view> allowed) {
    auto value = optionalStringField(body, field, label);
    if (!value) {
        return std::nullopt;
    }

    if (!matchesAllowed(*value, allowed)) {
        throw ValidationException("请选择" + label);
    }

    return value;
}

inline std::string requireEnumField(const Json::Value& body, const std::string& field,
                                    const std::string& label,
                                    std::initializer_list<std::string_view> allowed) {
    auto value = optionalEnumField(body, field, label, allowed);
    if (!value) {
        throw ValidationException("请选择" + label);
    }
    return *value;
}

inline std::optional<int> optionalPositiveIntField(const Json::Value& body,
                                                   const std::string& field,
                                                   const std::string& label) {
    if (!body.isMember(field) || body[field].isNull()) {
        return std::nullopt;
    }

    return parsePositiveIntValue(body[field], label);
}

inline int requirePositiveIntField(const Json::Value& body, const std::string& field,
                                   const std::string& label) {
    auto value = optionalPositiveIntField(body, field, label);
    if (!value) {
        throw ValidationException("请输入" + label);
    }
    return *value;
}

inline std::optional<int> optionalNonNegativeIntField(const Json::Value& body,
                                                     const std::string& field,
                                                     const std::string& label) {
    if (!body.isMember(field)) {
        return std::nullopt;
    }

    if (body[field].isNull()) {
        throw ValidationException(label + "不能为空");
    }

    return parseNonNegativeIntValue(body[field], label);
}

inline int requireNonNegativeIntField(const Json::Value& body, const std::string& field,
                                      const std::string& label) {
    auto value = optionalNonNegativeIntField(body, field, label);
    if (!value) {
        throw ValidationException("请输入" + label);
    }
    return *value;
}

inline std::optional<bool> optionalBoolField(const Json::Value& body, const std::string& field,
                                            const std::string& label) {
    if (!body.isMember(field)) {
        return std::nullopt;
    }

    if (body[field].isNull()) {
        throw ValidationException(label + "不能为空");
    }

    if (!body[field].isBool()) {
        throw ValidationException(label + "必须是布尔值");
    }

    return body[field].asBool();
}

inline std::optional<std::vector<int>> optionalPositiveIntArrayField(
    const Json::Value& body, const std::string& field, const std::string& message,
    bool allowEmpty = false) {
    if (!body.isMember(field)) {
        return std::nullopt;
    }

    if (body[field].isNull()) {
        throw ValidationException(message);
    }

    if (!body[field].isArray()) {
        throw ValidationException(message);
    }

    std::vector<int> values;
    values.reserve(body[field].size());

    for (const auto& item : body[field]) {
        int parsed = 0;
        try {
            parsed = parsePositiveIntValue(item, message);
        } catch (const ValidationException&) {
            throw ValidationException(message);
        }

        values.push_back(parsed);
    }

    if (!allowEmpty && values.empty()) {
        throw ValidationException(message);
    }

    return values;
}

inline std::vector<int> requirePositiveIntArrayField(const Json::Value& body,
                                                     const std::string& field,
                                                     const std::string& message,
                                                     bool allowEmpty = false) {
    auto values = optionalPositiveIntArrayField(body, field, message, allowEmpty);
    if (!values) {
        throw ValidationException(message);
    }
    return *values;
}

inline std::string queryString(const drogon::HttpRequestPtr& req, const std::string& field) {
    return StringUtils::trim(req->getParameter(field));
}

inline std::optional<std::string> optionalQueryText(const drogon::HttpRequestPtr& req,
                                                    const std::string& field) {
    std::string value = queryString(req, field);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

inline std::optional<std::string> optionalEnumQuery(const drogon::HttpRequestPtr& req,
                                                    const std::string& field,
                                                    const std::string& label,
                                                    std::initializer_list<std::string_view> allowed) {
    std::string value = queryString(req, field);
    if (value.empty()) {
        return std::nullopt;
    }

    if (!matchesAllowed(value, allowed)) {
        throw ValidationException("请选择" + label);
    }

    return value;
}

inline std::optional<int> optionalPositiveIntQuery(const drogon::HttpRequestPtr& req,
                                                   const std::string& field,
                                                   const std::string& label) {
    std::string text = queryString(req, field);
    if (text.empty()) {
        return std::nullopt;
    }

    return parsePositiveIntText(text, label);
}

inline std::string requireBearerToken(const drogon::HttpRequestPtr& req) {
    std::string authorization = req->getHeader("Authorization");
    if (!StringUtils::startsWith(authorization, "Bearer ")) {
        throw ValidationException("Token 格式错误");
    }

    std::string token = StringUtils::trim(authorization.substr(7));
    if (token.empty()) {
        throw ValidationException("Token 格式错误");
    }

    return token;
}

}  // namespace RequestValidation
