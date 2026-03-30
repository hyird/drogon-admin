#pragma once

#include <drogon/HttpController.h>
#include "Department.Service.hpp"
#include "common/utils/Response.hpp"
#include "common/utils/AppException.hpp"
#include "common/filters/PermissionFilter.hpp"

using namespace drogon;

/**
 * @brief 部门管理控制器
 */
class DepartmentController : public HttpController<DepartmentController> {
private:
    DepartmentService service_;

public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DepartmentController::list, "/api/departments", Get, "AuthFilter");
    ADD_METHOD_TO(DepartmentController::tree, "/api/departments/tree", Get, "AuthFilter");
    ADD_METHOD_TO(DepartmentController::detail, "/api/departments/{id}", Get, "AuthFilter");
    ADD_METHOD_TO(DepartmentController::create, "/api/departments", Post, "AuthFilter");
    ADD_METHOD_TO(DepartmentController::update, "/api/departments/{id}", Put, "AuthFilter");
    ADD_METHOD_TO(DepartmentController::remove, "/api/departments/{id}", Delete, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> list(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:dept:query"});

            auto query = SystemRequests::makeDepartmentListQuery(req);
            auto items = co_await service_.list(query);
            co_return Response::ok(items, SystemHelpers::departmentRecordItemsToJson);
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "DepartmentController::list error: " << e.what();
            co_return Response::internalError("获取部门列表失败");
        }
    }

    Task<HttpResponsePtr> tree(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:dept:query"});

            auto query = SystemRequests::makeDepartmentTreeQuery(req);
            auto tree = co_await service_.tree(query);
            co_return Response::ok(tree, [](const auto& nodes) {
                return TreeBuilder::toJson(nodes, [](const auto& item) { return SystemHelpers::departmentRecordToJson(item); });
            });
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "DepartmentController::tree error: " << e.what();
            co_return Response::internalError("获取部门树失败");
        }
    }

    Task<HttpResponsePtr> detail(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:dept:query"});

            co_return Response::ok(co_await service_.detail(id), SystemHelpers::departmentRecordToJson);
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "DepartmentController::detail error: " << e.what();
            co_return Response::internalError("获取部门详情失败");
        }
    }

    Task<HttpResponsePtr> create(HttpRequestPtr req) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:dept:add"});

            co_await service_.create(SystemRequests::makeDepartmentCreateRequest(req));
            co_return Response::created("创建成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "DepartmentController::create error: " << e.what();
            co_return Response::internalError("创建部门失败");
        }
    }

    Task<HttpResponsePtr> update(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:dept:edit"});

            co_await service_.update(id, SystemRequests::makeDepartmentUpdateRequest(req));
            co_return Response::updated("更新成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "DepartmentController::update error: " << e.what();
            co_return Response::internalError("更新部门失败");
        }
    }

    Task<HttpResponsePtr> remove(HttpRequestPtr req, int id) {
        try {
            int userId = req->attributes()->get<int>("userId");
            co_await PermissionChecker::checkPermission(userId, {"system:dept:delete"});

            co_await service_.remove(id);
            co_return Response::deleted("删除成功");
        } catch (const AppException& e) {
            co_return Response::error(e.getCode(), e.getMessage(), e.getStatus());
        } catch (const std::exception& e) {
            LOG_ERROR << "DepartmentController::remove error: " << e.what();
            co_return Response::internalError("删除部门失败");
        }
    }
};
