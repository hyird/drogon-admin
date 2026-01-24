#pragma once

#include <drogon/HttpController.h>
#include "Role.Service.hpp"
#include "common/utils/Response.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/AppException.hpp"
#include "common/filters/PermissionFilter.hpp"

using namespace drogon;

/**
 * @brief 角色管理控制器
 */
class RoleController : public HttpController<RoleController> {
private:
    RoleService service_;

public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(RoleController::list, "/api/roles", Get, "AuthFilter");
    ADD_METHOD_TO(RoleController::all, "/api/roles/all", Get, "AuthFilter");
    ADD_METHOD_TO(RoleController::detail, "/api/roles/{id}", Get, "AuthFilter");
    ADD_METHOD_TO(RoleController::create, "/api/roles", Post, "AuthFilter");
    ADD_METHOD_TO(RoleController::update, "/api/roles/{id}", Put, "AuthFilter");
    ADD_METHOD_TO(RoleController::remove, "/api/roles/{id}", Delete, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> list(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:role:query"});

            auto page = Pagination::fromRequest(req);
            auto [items, total] = co_await service_.list(page, req->getParameter("status"));
            co_return Pagination::buildResponse(items, total, page.page, page.pageSize);
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "RoleController::list error: " << e.what();
            co_return Response::internalError("获取角色列表失败");
        }
    }

    Task<HttpResponsePtr> all(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:role:query"});

            co_return Response::ok(co_await service_.all());
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "RoleController::all error: " << e.what();
            co_return Response::internalError("获取角色列表失败");
        }
    }

    Task<HttpResponsePtr> detail(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:role:query"});

            co_return Response::ok(co_await service_.detail(id));
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "RoleController::detail error: " << e.what();
            co_return Response::internalError("获取角色详情失败");
        }
    }

    Task<HttpResponsePtr> create(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:role:add"});

            auto json = req->getJsonObject();
            if (!json) co_return Response::badRequest("请求体格式错误");
            if (!json->isMember("name") || (*json)["name"].asString().empty())
                co_return Response::badRequest("角色名称不能为空");
            if (!json->isMember("code") || (*json)["code"].asString().empty())
                co_return Response::badRequest("角色编码不能为空");
            co_await service_.create(*json);
            co_return Response::created("创建成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "RoleController::create error: " << e.what();
            co_return Response::internalError("创建角色失败");
        }
    }

    Task<HttpResponsePtr> update(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:role:edit"});

            auto json = req->getJsonObject();
            if (!json) co_return Response::badRequest("请求体格式错误");
            co_await service_.update(id, *json);
            co_return Response::updated("更新成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "RoleController::update error: " << e.what();
            co_return Response::internalError("更新角色失败");
        }
    }

    Task<HttpResponsePtr> remove(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:role:delete"});

            co_await service_.remove(id);
            co_return Response::deleted("删除成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "RoleController::remove error: " << e.what();
            co_return Response::internalError("删除角色失败");
        }
    }
};
