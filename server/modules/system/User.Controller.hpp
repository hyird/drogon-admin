#pragma once

#include <drogon/HttpController.h>
#include "User.Service.hpp"
#include "common/utils/Response.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/AppException.hpp"
#include "common/filters/PermissionFilter.hpp"

using namespace drogon;

/**
 * @brief 用户管理控制器
 */
class UserController : public HttpController<UserController> {
private:
    UserService service_;

public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::list, "/api/users", Get, "AuthFilter");
    ADD_METHOD_TO(UserController::detail, "/api/users/{id}", Get, "AuthFilter");
    ADD_METHOD_TO(UserController::create, "/api/users", Post, "AuthFilter");
    ADD_METHOD_TO(UserController::update, "/api/users/{id}", Put, "AuthFilter");
    ADD_METHOD_TO(UserController::remove, "/api/users/{id}", Delete, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> list(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:user:query"});

            auto page = Pagination::fromRequest(req);
            int departmentId = 0;
            auto deptStr = req->getParameter("departmentId");
            if (!deptStr.empty()) try { departmentId = std::stoi(deptStr); } catch (...) {}

            auto [items, total] = co_await service_.list(page, req->getParameter("status"), departmentId);
            co_return Pagination::buildResponse(items, total, page.page, page.pageSize);
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "UserController::list error: " << e.what();
            co_return Response::internalError("获取用户列表失败");
        }
    }

    Task<HttpResponsePtr> detail(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:user:query"});

            co_return Response::ok(co_await service_.detail(id));
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "UserController::detail error: " << e.what();
            co_return Response::internalError("获取用户详情失败");
        }
    }

    Task<HttpResponsePtr> create(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:user:add"});

            auto json = req->getJsonObject();
            if (!json) co_return Response::badRequest("请求体格式错误");
            if (!json->isMember("username") || (*json)["username"].asString().empty())
                co_return Response::badRequest("用户名不能为空");
            if (!json->isMember("password") || (*json)["password"].asString().empty())
                co_return Response::badRequest("密码不能为空");
            if ((*json)["password"].asString().length() < 6)
                co_return Response::badRequest("密码长度不能小于6位");
            co_await service_.create(*json);
            co_return Response::created("创建成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "UserController::create error: " << e.what();
            co_return Response::internalError("创建用户失败");
        }
    }

    Task<HttpResponsePtr> update(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:user:edit"});

            auto json = req->getJsonObject();
            if (!json) co_return Response::badRequest("请求体格式错误");
            if (json->isMember("password") && !(*json)["password"].asString().empty() && (*json)["password"].asString().length() < 6)
                co_return Response::badRequest("密码长度不能小于6位");
            co_await service_.update(id, *json);
            co_return Response::updated("更新成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "UserController::update error: " << e.what();
            co_return Response::internalError("更新用户失败");
        }
    }

    Task<HttpResponsePtr> remove(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:user:delete"});

            if (userId == id)
                co_return Response::badRequest("不能删除当前登录用户");
            co_await service_.remove(id);
            co_return Response::deleted("删除成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "UserController::remove error: " << e.what();
            co_return Response::internalError("删除用户失败");
        }
    }
};
