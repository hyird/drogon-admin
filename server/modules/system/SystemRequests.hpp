#pragma once

#include <optional>
#include <string>
#include <vector>
#include <utility>

#include <json/json.h>

#include "common/utils/Pagination.hpp"
#include "common/utils/RequestValidation.hpp"

namespace SystemRequests {

struct UserCreateRequest {
    std::string username;
    std::string password;
    std::optional<std::string> nickname;
    std::optional<std::string> phone;
    std::optional<std::string> email;
    std::optional<int> departmentId;
    std::string status;
    std::vector<int> roleIds;
};

struct UserUpdateRequest {
    std::optional<std::string> nickname;
    std::optional<std::string> phone;
    std::optional<std::string> email;
    std::optional<std::optional<int>> departmentId;
    std::optional<std::string> status;
    std::optional<std::string> password;
    std::optional<std::vector<int>> roleIds;
};

struct RoleCreateRequest {
    std::string name;
    std::string code;
    std::optional<std::string> description;
    std::string status;
    std::optional<std::vector<int>> menuIds;
};

struct RoleUpdateRequest {
    std::optional<std::string> name;
    std::optional<std::string> code;
    std::optional<std::string> description;
    std::optional<std::string> status;
    std::optional<std::vector<int>> menuIds;
};

struct DepartmentCreateRequest {
    std::string name;
    std::optional<std::string> code;
    std::optional<int> parentId;
    std::optional<int> order;
    std::optional<int> leaderId;
    std::string status;
};

struct DepartmentUpdateRequest {
    std::optional<std::string> name;
    std::optional<std::string> code;
    std::optional<std::optional<int>> parentId;
    std::optional<std::optional<int>> leaderId;
    std::optional<int> order;
    std::optional<std::string> status;
};

struct MenuCreateRequest {
    std::string name;
    std::optional<std::string> path;
    std::optional<std::string> icon;
    std::optional<int> parentId;
    std::optional<int> order;
    std::string type;
    std::optional<std::string> component;
    std::string status;
    std::optional<std::string> permissionCode;
    std::optional<bool> isDefault;
};

struct MenuUpdateRequest {
    std::optional<std::string> name;
    std::optional<std::string> path;
    std::optional<std::string> icon;
    std::optional<std::optional<int>> parentId;
    std::optional<int> order;
    std::optional<std::string> type;
    std::optional<std::string> component;
    std::optional<std::string> status;
    std::optional<std::string> permissionCode;
    std::optional<bool> isDefault;
};

struct UserListQuery {
    Pagination pagination;
    std::optional<std::string> status;
    std::optional<int> departmentId;
};

struct RoleListQuery {
    Pagination pagination;
    std::optional<std::string> status;
};

struct MenuListQuery {
    std::optional<std::string> keyword;
    std::optional<std::string> status;
};

struct MenuTreeQuery {
    std::optional<std::string> status;
};

struct DepartmentListQuery {
    std::optional<std::string> keyword;
    std::optional<std::string> status;
};

struct DepartmentTreeQuery {
    std::optional<std::string> status;
};

inline std::optional<std::string> optionalText(const Json::Value& json,
                                               const char* field,
                                               const std::string& label,
                                               size_t minLength = 0,
                                               size_t maxLength = 0) {
    return RequestValidation::optionalStringField(json, field, label, minLength, maxLength);
}

inline std::optional<int> optionalPositiveInt(const Json::Value& json,
                                              const char* field,
                                              const std::string& label) {
    return RequestValidation::optionalPositiveIntField(json, field, label);
}

inline std::optional<std::optional<int>> nullablePositiveInt(const Json::Value& json,
                                                             const char* field,
                                                             const std::string& label) {
    if (!json.isMember(field)) {
        return std::nullopt;
    }

    if (json[field].isNull()) {
        return std::optional<int>{};
    }

    return RequestValidation::optionalPositiveIntField(json, field, label);
}

inline std::optional<int> optionalNonNegativeInt(const Json::Value& json,
                                                 const char* field,
                                                 const std::string& label) {
    return RequestValidation::optionalNonNegativeIntField(json, field, label);
}

inline std::optional<bool> optionalBool(const Json::Value& json, const char* field,
                                       const std::string& label) {
    return RequestValidation::optionalBoolField(json, field, label);
}

inline std::optional<std::vector<int>> optionalPositiveIntArray(const Json::Value& json,
                                                                const char* field,
                                                                const std::string& message,
                                                                bool allowEmpty = false) {
    return RequestValidation::optionalPositiveIntArrayField(json, field, message, allowEmpty);
}

inline std::vector<int> requiredPositiveIntArray(const Json::Value& json,
                                                 const char* field,
                                                 const std::string& message,
                                                 bool allowEmpty = false) {
    return RequestValidation::requirePositiveIntArrayField(json, field, message, allowEmpty);
}

inline UserCreateRequest makeUserCreateRequest(const Json::Value& json) {
    UserCreateRequest request;
    request.username = RequestValidation::requireStringField(json, "username", "用户名", 1, 50);
    request.password = RequestValidation::requireStringField(json, "password", "密码", 6, 100);
    request.nickname = optionalText(json, "nickname", "昵称", 0, 50);

    if (auto phone = optionalText(json, "phone", "手机号", 0, 20)) {
        RequestValidation::requirePattern(*phone, RequestValidation::PHONE_REGEX,
                                          "请输入正确的手机号");
        request.phone = std::move(phone);
    }

    if (auto email = optionalText(json, "email", "邮箱", 0, 100)) {
        RequestValidation::requirePattern(*email, RequestValidation::EMAIL_REGEX,
                                          "请输入正确的邮箱");
        request.email = std::move(email);
    }

    request.departmentId = optionalPositiveInt(json, "departmentId", "部门ID");
    request.status = RequestValidation::requireEnumField(json, "status", "状态", {"enabled", "disabled"});
    request.roleIds = requiredPositiveIntArray(json, "roleIds", "请选择至少一个角色");
    return request;
}

inline UserUpdateRequest makeUserUpdateRequest(const Json::Value& json) {
    RequestValidation::requireAnyField(json,
                                       {"nickname", "phone", "email", "departmentId",
                                        "status", "password", "roleIds"});

    UserUpdateRequest request;
    if (json.isMember("nickname")) {
        request.nickname = optionalText(json, "nickname", "昵称", 0, 50);
    }
    if (json.isMember("phone")) {
        auto phone = optionalText(json, "phone", "手机号", 0, 20);
        if (phone) {
            RequestValidation::requirePattern(*phone, RequestValidation::PHONE_REGEX,
                                              "请输入正确的手机号");
        }
        request.phone = std::move(phone);
    }
    if (json.isMember("email")) {
        auto email = optionalText(json, "email", "邮箱", 0, 100);
        if (email) {
            RequestValidation::requirePattern(*email, RequestValidation::EMAIL_REGEX,
                                              "请输入正确的邮箱");
        }
        request.email = std::move(email);
    }
    if (json.isMember("departmentId")) {
        request.departmentId = nullablePositiveInt(json, "departmentId", "部门ID");
    }
    if (json.isMember("status")) {
        request.status = RequestValidation::optionalEnumField(json, "status", "状态", {"enabled", "disabled"});
    }
    if (json.isMember("password")) {
        request.password = optionalText(json, "password", "密码", 6, 100);
    }
    if (json.isMember("roleIds")) {
        request.roleIds = optionalPositiveIntArray(json, "roleIds", "请选择至少一个角色");
    }
    return request;
}

inline RoleCreateRequest makeRoleCreateRequest(const Json::Value& json) {
    RoleCreateRequest request;
    request.name = RequestValidation::requireStringField(json, "name", "角色名称", 1, 50);
    request.code = RequestValidation::requireStringField(json, "code", "角色编码", 1, 50);
    RequestValidation::requirePattern(request.code, RequestValidation::ROLE_CODE_REGEX,
                                      "编码以字母开头，只能包含字母、数字、下划线");
    request.description = optionalText(json, "description", "描述", 0, 255);
    request.status = RequestValidation::requireEnumField(json, "status", "状态", {"enabled", "disabled"});
    if (json.isMember("menuIds")) {
        request.menuIds = optionalPositiveIntArray(json, "menuIds", "菜单 ID 列表无效", true);
    }
    return request;
}

inline RoleUpdateRequest makeRoleUpdateRequest(const Json::Value& json) {
    RequestValidation::requireAnyField(json, {"name", "code", "description", "status", "menuIds"});

    RoleUpdateRequest request;
    if (json.isMember("name")) {
        request.name = optionalText(json, "name", "角色名称", 1, 50);
    }
    if (json.isMember("code")) {
        auto code = RequestValidation::requireStringField(json, "code", "角色编码", 1, 50);
        RequestValidation::requirePattern(code, RequestValidation::ROLE_CODE_REGEX,
                                          "编码以字母开头，只能包含字母、数字、下划线");
        request.code = std::move(code);
    }
    if (json.isMember("description")) {
        request.description = optionalText(json, "description", "描述", 0, 255);
    }
    if (json.isMember("status")) {
        request.status = RequestValidation::optionalEnumField(json, "status", "状态", {"enabled", "disabled"});
    }
    if (json.isMember("menuIds")) {
        request.menuIds = optionalPositiveIntArray(json, "menuIds", "菜单 ID 列表无效", true);
    }
    return request;
}

inline DepartmentCreateRequest makeDepartmentCreateRequest(const Json::Value& json) {
    DepartmentCreateRequest request;
    request.name = RequestValidation::requireStringField(json, "name", "部门名称", 1, 50);
    request.code = optionalText(json, "code", "部门编码", 0, 50);
    request.parentId = optionalPositiveInt(json, "parentId", "上级部门");
    request.order = optionalNonNegativeInt(json, "order", "排序");
    request.leaderId = optionalPositiveInt(json, "leaderId", "负责人");
    request.status = RequestValidation::requireEnumField(json, "status", "状态", {"enabled", "disabled"});
    return request;
}

inline DepartmentUpdateRequest makeDepartmentUpdateRequest(const Json::Value& json) {
    RequestValidation::requireAnyField(json, {"name", "code", "parentId", "leaderId", "order", "status"});

    DepartmentUpdateRequest request;
    if (json.isMember("name")) {
        request.name = optionalText(json, "name", "部门名称", 1, 50);
    }
    if (json.isMember("code")) {
        request.code = optionalText(json, "code", "部门编码", 0, 50);
    }
    if (json.isMember("parentId")) {
        request.parentId = nullablePositiveInt(json, "parentId", "上级部门");
    }
    if (json.isMember("leaderId")) {
        request.leaderId = nullablePositiveInt(json, "leaderId", "负责人");
    }
    if (json.isMember("order")) {
        request.order = optionalNonNegativeInt(json, "order", "排序");
    }
    if (json.isMember("status")) {
        request.status = RequestValidation::optionalEnumField(json, "status", "状态", {"enabled", "disabled"});
    }
    return request;
}

inline MenuCreateRequest makeMenuCreateRequest(const Json::Value& json) {
    MenuCreateRequest request;
    request.name = RequestValidation::requireStringField(json, "name", "名称", 1, 50);
    request.type = RequestValidation::requireEnumField(json, "type", "类型", {"menu", "page", "button"});
    request.status = RequestValidation::requireEnumField(json, "status", "状态", {"enabled", "disabled"});
    request.parentId = optionalPositiveInt(json, "parentId", "父级菜单");
    request.order = optionalNonNegativeInt(json, "order", "排序");
    request.icon = optionalText(json, "icon", "图标", 0, 100);
    request.isDefault = optionalBool(json, "isDefault", "是否默认");

    if (request.type == "page") {
        request.path = RequestValidation::requireStringField(json, "path", "路径", 1, 255);
        request.component = RequestValidation::requireStringField(json, "component", "组件标识", 1, 255);
    } else if (request.type == "button") {
        request.permissionCode = RequestValidation::requireStringField(json, "permissionCode", "权限标识", 1, 100);
    } else {
        if (json.isMember("path")) {
            request.path = optionalText(json, "path", "路径", 0, 255);
        }
        if (json.isMember("component")) {
            request.component = optionalText(json, "component", "组件标识", 0, 255);
        }
        if (json.isMember("permissionCode")) {
            request.permissionCode = optionalText(json, "permissionCode", "权限标识", 0, 100);
        }
    }

    return request;
}

inline MenuUpdateRequest makeMenuUpdateRequest(const Json::Value& json) {
    RequestValidation::requireAnyField(
        json, {"name", "path", "icon", "parentId", "order", "type", "component",
               "status", "permissionCode", "isDefault"});

    MenuUpdateRequest request;
    if (json.isMember("type")) {
        request.type = RequestValidation::optionalEnumField(json, "type", "类型", {"menu", "page", "button"});
    }
    if (json.isMember("name")) {
        request.name = optionalText(json, "name", "名称", 1, 50);
    }
    if (json.isMember("path")) {
        request.path = optionalText(json, "path", "路径", 0, 255);
    }
    if (json.isMember("icon")) {
        request.icon = optionalText(json, "icon", "图标", 0, 100);
    }
    if (json.isMember("parentId")) {
        request.parentId = nullablePositiveInt(json, "parentId", "父级菜单");
    }
    if (json.isMember("order")) {
        request.order = optionalNonNegativeInt(json, "order", "排序");
    }
    if (json.isMember("component")) {
        request.component = optionalText(json, "component", "组件标识", 0, 255);
    }
    if (json.isMember("status")) {
        request.status = RequestValidation::optionalEnumField(json, "status", "状态", {"enabled", "disabled"});
    }
    if (json.isMember("permissionCode")) {
        request.permissionCode = optionalText(json, "permissionCode", "权限标识", 0, 100);
    }
    if (json.isMember("isDefault")) {
        request.isDefault = optionalBool(json, "isDefault", "是否默认");
    }

    if (request.type) {
        if (*request.type == "page") {
            request.path = RequestValidation::requireStringField(json, "path", "路径", 1, 255);
            request.component = RequestValidation::requireStringField(json, "component", "组件标识", 1, 255);
        } else if (*request.type == "button") {
            request.permissionCode = RequestValidation::requireStringField(json, "permissionCode", "权限标识", 1, 100);
        }
    }

    return request;
}

inline UserListQuery makeUserListQuery(const drogon::HttpRequestPtr& req) {
    UserListQuery query;
    query.pagination = Pagination::fromRequest(req);
    query.status = RequestValidation::optionalEnumQuery(req, "status", "状态", {"enabled", "disabled"});
    query.departmentId = RequestValidation::optionalPositiveIntQuery(req, "departmentId", "部门ID");
    return query;
}

inline RoleListQuery makeRoleListQuery(const drogon::HttpRequestPtr& req) {
    RoleListQuery query;
    query.pagination = Pagination::fromRequest(req);
    query.status = RequestValidation::optionalEnumQuery(req, "status", "状态", {"enabled", "disabled"});
    return query;
}

inline MenuListQuery makeMenuListQuery(const drogon::HttpRequestPtr& req) {
    MenuListQuery query;
    query.keyword = RequestValidation::optionalQueryText(req, "keyword");
    query.status = RequestValidation::optionalEnumQuery(req, "status", "状态", {"enabled", "disabled"});
    return query;
}

inline MenuTreeQuery makeMenuTreeQuery(const drogon::HttpRequestPtr& req) {
    MenuTreeQuery query;
    query.status = RequestValidation::optionalEnumQuery(req, "status", "状态", {"enabled", "disabled"});
    return query;
}

inline DepartmentListQuery makeDepartmentListQuery(const drogon::HttpRequestPtr& req) {
    DepartmentListQuery query;
    query.keyword = RequestValidation::optionalQueryText(req, "keyword");
    query.status = RequestValidation::optionalEnumQuery(req, "status", "状态", {"enabled", "disabled"});
    return query;
}

inline DepartmentTreeQuery makeDepartmentTreeQuery(const drogon::HttpRequestPtr& req) {
    DepartmentTreeQuery query;
    query.status = RequestValidation::optionalEnumQuery(req, "status", "状态", {"enabled", "disabled"});
    return query;
}

inline UserCreateRequest makeUserCreateRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeUserCreateRequest(body);
    });
}

inline UserUpdateRequest makeUserUpdateRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeUserUpdateRequest(body);
    });
}

inline RoleCreateRequest makeRoleCreateRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeRoleCreateRequest(body);
    });
}

inline RoleUpdateRequest makeRoleUpdateRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeRoleUpdateRequest(body);
    });
}

inline DepartmentCreateRequest makeDepartmentCreateRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeDepartmentCreateRequest(body);
    });
}

inline DepartmentUpdateRequest makeDepartmentUpdateRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeDepartmentUpdateRequest(body);
    });
}

inline MenuCreateRequest makeMenuCreateRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeMenuCreateRequest(body);
    });
}

inline MenuUpdateRequest makeMenuUpdateRequest(const drogon::HttpRequestPtr& req) {
    return RequestValidation::makeJsonBodyRequest(req, [](const Json::Value& body) {
        return makeMenuUpdateRequest(body);
    });
}

}  // namespace SystemRequests
