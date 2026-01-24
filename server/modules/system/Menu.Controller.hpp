#pragma once

#include <drogon/HttpController.h>
#include "Menu.Service.hpp"
#include "common/utils/Response.hpp"
#include "common/utils/AppException.hpp"
#include "common/filters/PermissionFilter.hpp"

using namespace drogon;

/**
 * @brief 菜单管理控制器
 */
class MenuController : public HttpController<MenuController> {
private:
    MenuService service_;

public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MenuController::list, "/api/menus", Get, "AuthFilter");
    ADD_METHOD_TO(MenuController::tree, "/api/menus/tree", Get, "AuthFilter");
    ADD_METHOD_TO(MenuController::detail, "/api/menus/{id}", Get, "AuthFilter");
    ADD_METHOD_TO(MenuController::create, "/api/menus", Post, "AuthFilter");
    ADD_METHOD_TO(MenuController::update, "/api/menus/{id}", Put, "AuthFilter");
    ADD_METHOD_TO(MenuController::remove, "/api/menus/{id}", Delete, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> list(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:menu:query"});

            auto items = co_await service_.list(req->getParameter("keyword"), req->getParameter("status"));
            co_return Response::ok(items);
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "MenuController::list error: " << e.what();
            co_return Response::internalError("获取菜单列表失败");
        }
    }

    Task<HttpResponsePtr> tree(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:menu:query"});

            auto tree = co_await service_.tree(req->getParameter("status"));
            co_return Response::ok(tree);
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "MenuController::tree error: " << e.what();
            co_return Response::internalError("获取菜单树失败");
        }
    }

    Task<HttpResponsePtr> detail(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:menu:query"});

            co_return Response::ok(co_await service_.detail(id));
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "MenuController::detail error: " << e.what();
            co_return Response::internalError("获取菜单详情失败");
        }
    }

    Task<HttpResponsePtr> create(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:menu:add"});

            auto json = req->getJsonObject();
            if (!json) co_return Response::badRequest("请求体格式错误");
            if (!json->isMember("name") || (*json)["name"].asString().empty())
                co_return Response::badRequest("菜单名称不能为空");
            co_await service_.create(*json);
            co_return Response::created("创建成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "MenuController::create error: " << e.what();
            co_return Response::internalError("创建菜单失败");
        }
    }

    Task<HttpResponsePtr> update(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:menu:edit"});

            auto json = req->getJsonObject();
            if (!json) co_return Response::badRequest("请求体格式错误");
            co_await service_.update(id, *json);
            co_return Response::updated("更新成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "MenuController::update error: " << e.what();
            co_return Response::internalError("更新菜单失败");
        }
    }

    Task<HttpResponsePtr> remove(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:menu:delete"});

            co_await service_.remove(id);
            co_return Response::deleted("删除成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "MenuController::remove error: " << e.what();
            co_return Response::internalError("删除菜单失败");
        }
    }
};
