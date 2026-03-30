#pragma once

#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <string>

using namespace drogon;

/**
 * @brief 统一响应格式工具类
 */
class Response {
public:
    template <typename T, typename ToJson>
    static HttpResponsePtr ok(const T& data,
                              ToJson convert,
                              const std::string& message = "Success") {
        return buildResponse(0, message, convert(data), k200OK);
    }

    static HttpResponsePtr success(const std::string& message = "Success") {
        return buildResponse(0, message, k200OK);
    }

    static HttpResponsePtr created(const std::string &message = "创建成功") {
        return buildResponse(0, message, k201Created);
    }

    static HttpResponsePtr updated(const std::string &message = "更新成功") {
        return buildResponse(0, message, k200OK);
    }

    static HttpResponsePtr deleted(const std::string &message = "删除成功") {
        return buildResponse(0, message, k200OK);
    }

    static HttpResponsePtr error(int code,
                                   const std::string &message,
                                   HttpStatusCode status = k400BadRequest) {
        return buildResponse(code, message, status);
    }

    static HttpResponsePtr unauthorized(const std::string &message = "未授权访问") {
        return error(2004, message, k401Unauthorized);
    }

    static HttpResponsePtr forbidden(const std::string &message = "无权限访问") {
        return error(1003, message, k403Forbidden);
    }

    static HttpResponsePtr notFound(const std::string &message = "资源不存在") {
        return error(1001, message, k404NotFound);
    }

    static HttpResponsePtr badRequest(const std::string &message = "请求参数错误") {
        return error(1002, message, k400BadRequest);
    }

    static HttpResponsePtr internalError(const std::string &message = "服务器内部错误") {
        return error(5000, message, k500InternalServerError);
    }

private:
    static HttpResponsePtr buildResponse(int code,
                                         const std::string& message,
                                         HttpStatusCode status) {
        Json::Value json;
        json["code"] = code;
        json["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(status);
        return resp;
    }

    static HttpResponsePtr buildResponse(int code,
                                         const std::string& message,
                                         const Json::Value& data,
                                         HttpStatusCode status) {
        Json::Value json;
        json["code"] = code;
        json["message"] = message;
        if (!data.isNull()) {
            json["data"] = data;
        }

        auto resp = HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(status);
        return resp;
    }
};
