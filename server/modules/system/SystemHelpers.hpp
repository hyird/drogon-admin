#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <drogon/orm/Row.h>
#include <json/json.h>

#include "common/utils/FieldHelper.hpp"
#include "SystemConstants.hpp"

namespace SystemHelpers {

struct RoleSummary {
    int id = 0;
    std::string name;
    std::string code;
};

struct RoleRecordSummary {
    int id = 0;
    std::string name;
    std::string code;
    std::string description;
    std::string status;
    std::string createdAt;
    std::string updatedAt;
};

struct UserRecordSummary {
    int id = 0;
    std::string username;
    std::string nickname;
    std::string phone;
    std::string email;
    int departmentId = 0;
    std::string departmentName;
    std::string status;
    std::string createdAt;
    std::string updatedAt;
};

struct DepartmentRecordSummary {
    int id = 0;
    std::string name;
    std::string code;
    int parentId = 0;
    int order = 0;
    int leaderId = 0;
    std::string status;
    std::string createdAt;
    std::string updatedAt;
};

struct MenuRecordSummary {
    int id = 0;
    std::string name;
    std::string path;
    std::string icon;
    int parentId = 0;
    int order = 0;
    std::string type;
    std::string component;
    std::string status;
    std::string permissionCode;
    bool isDefault = false;
    std::string createdAt;
    std::string updatedAt;
};

struct RoleMenuSummary {
    int id = 0;
    std::string name;
    std::string type;
    int parentId = 0;
};

struct MenuSummary {
    int id = 0;
    std::string name;
    int parentId = 0;
    std::string type;
    std::string path;
    std::string component;
    std::string permissionCode;
    std::string icon;
    int order = 0;
    bool visible = false;
};

struct CurrentUserSummary {
    int id = 0;
    std::string username;
    std::string nickname;
    std::string status;
    std::vector<RoleSummary> roles;
    std::vector<MenuSummary> menus;
};

struct UserListItemSummary {
    UserRecordSummary user;
    std::vector<RoleSummary> roles;
};

struct UserDetailSummary {
    UserRecordSummary user;
    std::vector<RoleSummary> roles;
    std::vector<int> roleIds;
};

struct RoleListItemSummary {
    RoleRecordSummary role;
    std::vector<int> menuIds;
};

struct RoleDetailSummary {
    RoleRecordSummary role;
    std::vector<int> menuIds;
    std::vector<RoleMenuSummary> menus;
};

struct AuthTokenClaims {
    int userId = 0;
    std::string username;
    int64_t issuedAt = 0;
    int64_t expiresAt = 0;
};

struct AuthTokensSummary {
    std::string token;
    std::string refreshToken;
};

struct LoginResponseSummary {
    AuthTokensSummary tokens;
    CurrentUserSummary user;
};

template <typename T, typename Mapper>
inline Json::Value toJsonArray(const std::vector<T>& items, Mapper&& mapper) {
    Json::Value json(Json::arrayValue);
    auto&& fn = std::forward<Mapper>(mapper);
    for (const auto& item : items) {
        json.append(fn(item));
    }
    return json;
}

template <typename T, typename Mapper>
inline std::vector<T> fromJsonArray(const Json::Value& json, Mapper&& mapper) {
    std::vector<T> result;
    if (!json.isArray()) {
        return result;
    }

    auto&& fn = std::forward<Mapper>(mapper);
    result.reserve(json.size());
    for (const auto& item : json) {
        result.push_back(fn(item));
    }
    return result;
}

inline Json::Value roleToJson(const RoleSummary& role) {
    Json::Value json;
    json["id"] = role.id;
    json["name"] = role.name;
    json["code"] = role.code;
    return json;
}

inline Json::Value roleRecordToJson(const RoleRecordSummary& role) {
    Json::Value json;
    json["id"] = role.id;
    json["name"] = role.name;
    json["code"] = role.code;
    json["description"] = role.description;
    json["status"] = role.status;
    json["createdAt"] = role.createdAt;
    json["updatedAt"] = role.updatedAt;
    return json;
}

inline Json::Value rolesToJson(const std::vector<RoleSummary>& roles) {
    return toJsonArray(roles, [](const RoleSummary& role) {
        return roleToJson(role);
    });
}

inline Json::Value menuToJson(const MenuSummary& menu) {
    Json::Value json;
    json["id"] = menu.id;
    json["name"] = menu.name;
    json["parentId"] = menu.parentId;
    json["type"] = menu.type;
    json["path"] = menu.path;
    json["component"] = menu.component;
    json["permissionCode"] = menu.permissionCode;
    json["icon"] = menu.icon;
    json["order"] = menu.order;
    json["visible"] = menu.visible;
    return json;
}

inline Json::Value menuRecordToJson(const MenuRecordSummary& menu) {
    Json::Value json;
    json["id"] = menu.id;
    json["name"] = menu.name;
    json["path"] = menu.path;
    json["icon"] = menu.icon;
    json["parentId"] = menu.parentId;
    json["order"] = menu.order;
    json["type"] = menu.type;
    json["component"] = menu.component;
    json["status"] = menu.status;
    json["permissionCode"] = menu.permissionCode;
    json["isDefault"] = menu.isDefault;
    json["createdAt"] = menu.createdAt;
    json["updatedAt"] = menu.updatedAt;
    return json;
}

inline Json::Value menuRecordItemsToJson(const std::vector<MenuRecordSummary>& items) {
    return toJsonArray(items, [](const MenuRecordSummary& item) {
        return menuRecordToJson(item);
    });
}

inline Json::Value menusToJson(const std::vector<MenuSummary>& menus) {
    return toJsonArray(menus, [](const MenuSummary& menu) {
        return menuToJson(menu);
    });
}

inline Json::Value roleMenuToJson(const RoleMenuSummary& menu) {
    Json::Value json;
    json["id"] = menu.id;
    json["name"] = menu.name;
    json["type"] = menu.type;
    json["parentId"] = menu.parentId;
    return json;
}

inline Json::Value roleMenusToJson(const std::vector<RoleMenuSummary>& menus) {
    return toJsonArray(menus, [](const RoleMenuSummary& menu) {
        return roleMenuToJson(menu);
    });
}

inline Json::Value intArrayToJson(const std::vector<int>& values) {
    return toJsonArray(values, [](int value) {
        Json::Value json(value);
        return json;
    });
}

inline std::vector<RoleSummary> rolesFromJson(const Json::Value& roles) {
    return fromJsonArray<RoleSummary>(roles, [](const Json::Value& item) {
        RoleSummary role;
        role.id = item.isMember("id") ? item["id"].asInt() : 0;
        role.name = item.isMember("name") ? item["name"].asString() : "";
        role.code = item.isMember("code") ? item["code"].asString() : "";
        return role;
    });
}

inline RoleRecordSummary roleRecordFromRow(const drogon::orm::Row& row) {
    RoleRecordSummary role;
    role.id = F_INT(row["id"]);
    role.name = F_STR(row["name"]);
    role.code = F_STR(row["code"]);
    role.description = F_STR_DEF(row["description"], "");
    role.status = F_STR_DEF(row["status"], "enabled");
    role.createdAt = F_STR_DEF(row["createdAt"], "");
    role.updatedAt = F_STR_DEF(row["updatedAt"], "");
    return role;
}

inline RoleSummary roleSummaryFromRow(const drogon::orm::Row& row) {
    RoleSummary role;
    role.id = F_INT(row["id"]);
    role.name = F_STR(row["name"]);
    role.code = F_STR(row["code"]);
    return role;
}

inline UserRecordSummary userRecordFromRow(const drogon::orm::Row& row) {
    UserRecordSummary user;
    user.id = F_INT(row["id"]);
    user.username = F_STR(row["username"]);
    user.nickname = F_STR_DEF(row["nickname"], "");
    user.phone = F_STR_DEF(row["phone"], "");
    user.email = F_STR_DEF(row["email"], "");
    user.departmentId = F_INT_DEF(row["departmentId"], 0);
    try {
        user.departmentName = F_STR_DEF(row["departmentName"], "");
    } catch (...) {
        user.departmentName = "";
    }
    user.status = F_STR_DEF(row["status"], "enabled");
    user.createdAt = F_STR_DEF(row["createdAt"], "");
    user.updatedAt = F_STR_DEF(row["updatedAt"], "");
    return user;
}

inline DepartmentRecordSummary departmentRecordFromRow(const drogon::orm::Row& row) {
    DepartmentRecordSummary department;
    department.id = F_INT(row["id"]);
    department.name = F_STR(row["name"]);
    department.code = F_STR_DEF(row["code"], "");
    department.parentId = F_INT_DEF(row["parentId"], 0);
    department.order = F_INT_DEF(row["order"], 0);
    department.leaderId = F_INT_DEF(row["leaderId"], 0);
    department.status = F_STR_DEF(row["status"], "enabled");
    department.createdAt = F_STR_DEF(row["createdAt"], "");
    department.updatedAt = F_STR_DEF(row["updatedAt"], "");
    return department;
}

inline MenuRecordSummary menuRecordFromRow(const drogon::orm::Row& row) {
    MenuRecordSummary menu;
    menu.id = F_INT(row["id"]);
    menu.name = F_STR(row["name"]);
    menu.path = F_STR_DEF(row["path"], "");
    menu.icon = F_STR_DEF(row["icon"], "");
    menu.parentId = F_INT_DEF(row["parentId"], 0);
    menu.order = F_INT_DEF(row["order"], 0);
    menu.type = F_STR_DEF(row["type"], "menu");
    menu.component = F_STR_DEF(row["component"], "");
    menu.status = F_STR_DEF(row["status"], "enabled");
    menu.permissionCode = F_STR_DEF(row["permissionCode"], "");
    menu.isDefault = F_INT_DEF(row["isDefault"], 0) == 1;
    menu.createdAt = F_STR_DEF(row["createdAt"], "");
    menu.updatedAt = F_STR_DEF(row["updatedAt"], "");
    return menu;
}

inline RoleMenuSummary roleMenuSummaryFromRow(const drogon::orm::Row& row) {
    RoleMenuSummary menu;
    menu.id = F_INT(row["id"]);
    menu.name = F_STR(row["name"]);
    menu.type = F_STR_DEF(row["type"], "menu");
    menu.parentId = F_INT_DEF(row["parentId"], 0);
    return menu;
}

inline MenuSummary menuSummaryFromRow(const drogon::orm::Row& row) {
    MenuSummary menu;
    menu.id = F_INT(row["id"]);
    menu.name = F_STR(row["name"]);
    menu.parentId = F_INT_DEF(row["parentId"], 0);
    menu.type = F_STR_DEF(row["type"], "menu");
    menu.path = F_STR_DEF(row["path"], "");
    menu.component = F_STR_DEF(row["component"], "");
    menu.permissionCode = F_STR_DEF(row["permissionCode"], "");
    menu.icon = F_STR_DEF(row["icon"], "");
    menu.order = F_INT_DEF(row["order"], 0);
    menu.visible = F_BOOL_DEF(row["visible"], false);
    return menu;
}

inline Json::Value userRecordToJson(const UserRecordSummary& user) {
    Json::Value json;
    json["id"] = user.id;
    json["username"] = user.username;
    json["nickname"] = user.nickname;
    json["phone"] = user.phone;
    json["email"] = user.email;
    json["departmentId"] = user.departmentId;
    json["departmentName"] = user.departmentName;
    json["status"] = user.status;
    json["createdAt"] = user.createdAt;
    json["updatedAt"] = user.updatedAt;
    return json;
}

inline Json::Value userListItemToJson(const UserListItemSummary& item) {
    Json::Value json = userRecordToJson(item.user);
    json["roles"] = rolesToJson(item.roles);
    return json;
}

inline Json::Value userListItemsToJson(const std::vector<UserListItemSummary>& items) {
    return toJsonArray(items, [](const UserListItemSummary& item) {
        return userListItemToJson(item);
    });
}

inline Json::Value userDetailToJson(const UserDetailSummary& item) {
    Json::Value json = userRecordToJson(item.user);
    json["roles"] = rolesToJson(item.roles);
    json["roleIds"] = intArrayToJson(item.roleIds);
    return json;
}

inline Json::Value departmentRecordToJson(const DepartmentRecordSummary& department) {
    Json::Value json;
    json["id"] = department.id;
    json["name"] = department.name;
    json["code"] = department.code;
    json["parentId"] = department.parentId;
    json["order"] = department.order;
    json["leaderId"] = department.leaderId;
    json["status"] = department.status;
    json["createdAt"] = department.createdAt;
    json["updatedAt"] = department.updatedAt;
    return json;
}

inline Json::Value departmentRecordItemsToJson(const std::vector<DepartmentRecordSummary>& items) {
    return toJsonArray(items, [](const DepartmentRecordSummary& item) {
        return departmentRecordToJson(item);
    });
}

inline Json::Value roleListItemToJson(const RoleListItemSummary& item) {
    Json::Value json = roleRecordToJson(item.role);
    json["menuIds"] = intArrayToJson(item.menuIds);
    return json;
}

inline Json::Value roleListItemsToJson(const std::vector<RoleListItemSummary>& items) {
    return toJsonArray(items, [](const RoleListItemSummary& item) {
        return roleListItemToJson(item);
    });
}

inline Json::Value roleDetailToJson(const RoleDetailSummary& item) {
    Json::Value json = roleRecordToJson(item.role);
    json["menuIds"] = intArrayToJson(item.menuIds);
    json["menus"] = roleMenusToJson(item.menus);
    return json;
}

inline std::vector<MenuSummary> menusFromJson(const Json::Value& menus) {
    return fromJsonArray<MenuSummary>(menus, [](const Json::Value& item) {
        MenuSummary menu;
        menu.id = item.isMember("id") ? item["id"].asInt() : 0;
        menu.name = item.isMember("name") ? item["name"].asString() : "";
        menu.parentId = item.isMember("parentId") ? item["parentId"].asInt() : 0;
        menu.type = item.isMember("type") ? item["type"].asString() : "";
        menu.path = item.isMember("path") ? item["path"].asString() : "";
        menu.component = item.isMember("component") ? item["component"].asString() : "";
        menu.permissionCode = item.isMember("permissionCode") ? item["permissionCode"].asString() : "";
        menu.icon = item.isMember("icon") ? item["icon"].asString() : "";
        menu.order = item.isMember("order") ? item["order"].asInt() : 0;
        menu.visible = item.isMember("visible") ? item["visible"].asBool() : false;
        return menu;
    });
}

inline Json::Value currentUserToJson(const CurrentUserSummary& user) {
    Json::Value json;
    json["id"] = user.id;
    json["username"] = user.username;
    json["nickname"] = user.nickname;
    json["status"] = user.status;
    json["roles"] = rolesToJson(user.roles);
    json["menus"] = menusToJson(user.menus);
    return json;
}

inline Json::Value authTokenClaimsToJson(const AuthTokenClaims& claims) {
    Json::Value json;
    json["userId"] = claims.userId;
    json["username"] = claims.username;
    if (claims.issuedAt > 0) {
        json["iat"] = Json::Value::Int64(claims.issuedAt);
    }
    if (claims.expiresAt > 0) {
        json["exp"] = Json::Value::Int64(claims.expiresAt);
    }
    return json;
}

inline AuthTokenClaims authTokenClaimsFromJson(const Json::Value& json) {
    AuthTokenClaims claims;
    claims.userId = json.isMember("userId") ? json["userId"].asInt() : 0;
    claims.username = json.isMember("username") ? json["username"].asString() : "";
    claims.issuedAt = json.isMember("iat") ? json["iat"].asInt64() : 0;
    claims.expiresAt = json.isMember("exp") ? json["exp"].asInt64() : 0;
    return claims;
}

inline Json::Value authTokensToJson(const AuthTokensSummary& tokens) {
    Json::Value json;
    json["token"] = tokens.token;
    json["refreshToken"] = tokens.refreshToken;
    return json;
}

inline Json::Value loginResponseToJson(const LoginResponseSummary& response) {
    Json::Value json;
    json["token"] = response.tokens.token;
    json["refreshToken"] = response.tokens.refreshToken;
    json["user"] = currentUserToJson(response.user);
    return json;
}

inline CurrentUserSummary currentUserFromJson(const Json::Value& json) {
    CurrentUserSummary user;
    user.id = json.isMember("id") ? json["id"].asInt() : 0;
    user.username = json.isMember("username") ? json["username"].asString() : "";
    user.nickname = json.isMember("nickname") ? json["nickname"].asString() : "";
    user.status = json.isMember("status") ? json["status"].asString() : "";
    user.roles = rolesFromJson(json.isMember("roles") ? json["roles"] : Json::Value(Json::arrayValue));
    user.menus = menusFromJson(json.isMember("menus") ? json["menus"] : Json::Value(Json::arrayValue));
    return user;
}

inline bool isRoleCode(const Json::Value& role, std::string_view code) {
    return role.isObject() && role.isMember("code") && !role["code"].isNull() &&
           role["code"].asString() == code;
}

inline bool isRoleCode(const RoleSummary& role, std::string_view code) {
    return role.code == code;
}

inline bool isRoleCode(const RoleRecordSummary& role, std::string_view code) {
    return role.code == code;
}

inline bool hasRoleCode(const Json::Value& roles, std::string_view code) {
    if (!roles.isArray()) {
        return false;
    }

    for (const auto& role : roles) {
        if (isRoleCode(role, code)) {
            return true;
        }
    }

    return false;
}

inline bool hasSuperadminRole(const Json::Value& roles) {
    return hasRoleCode(roles, SystemConstants::SUPERADMIN_ROLE_CODE);
}

inline bool hasRoleCode(const std::vector<RoleSummary>& roles, std::string_view code) {
    for (const auto& role : roles) {
        if (isRoleCode(role, code)) {
            return true;
        }
    }

    return false;
}

inline bool hasSuperadminRole(const std::vector<RoleSummary>& roles) {
    return hasRoleCode(roles, SystemConstants::SUPERADMIN_ROLE_CODE);
}

}  // namespace SystemHelpers
