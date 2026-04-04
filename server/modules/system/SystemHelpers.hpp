#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
    std::vector<std::string> permissionCodes;
    std::unordered_set<std::string> permissionCodeSet;
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

inline Json::Value nullablePositiveIntToJson(int value) {
    if (value > 0) {
        return Json::Value(value);
    }
    return Json::Value(Json::nullValue);
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

inline RoleRecordSummary roleRecordFromJson(const Json::Value& json) {
    RoleRecordSummary role;
    role.id = json.isMember("id") ? json["id"].asInt() : 0;
    role.name = json.isMember("name") ? json["name"].asString() : "";
    role.code = json.isMember("code") ? json["code"].asString() : "";
    role.description = json.isMember("description") ? json["description"].asString() : "";
    role.status = json.isMember("status") ? json["status"].asString() : "";
    role.createdAt = json.isMember("createdAt") ? json["createdAt"].asString() : "";
    role.updatedAt = json.isMember("updatedAt") ? json["updatedAt"].asString() : "";
    return role;
}

inline Json::Value roleRecordItemsToJson(const std::vector<RoleRecordSummary>& items) {
    return toJsonArray(items, [](const RoleRecordSummary& item) {
        return roleRecordToJson(item);
    });
}

inline std::vector<RoleRecordSummary> roleRecordItemsFromJson(const Json::Value& json) {
    return fromJsonArray<RoleRecordSummary>(json, [](const Json::Value& item) {
        return roleRecordFromJson(item);
    });
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
    json["parentId"] = nullablePositiveIntToJson(menu.parentId);
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
    json["parentId"] = nullablePositiveIntToJson(menu.parentId);
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

inline MenuRecordSummary menuRecordFromJson(const Json::Value& json) {
    MenuRecordSummary menu;
    menu.id = json.isMember("id") ? json["id"].asInt() : 0;
    menu.name = json.isMember("name") ? json["name"].asString() : "";
    menu.path = json.isMember("path") ? json["path"].asString() : "";
    menu.icon = json.isMember("icon") ? json["icon"].asString() : "";
    menu.parentId = json.isMember("parentId") ? json["parentId"].asInt() : 0;
    menu.order = json.isMember("order") ? json["order"].asInt() : 0;
    menu.type = json.isMember("type") ? json["type"].asString() : "";
    menu.component = json.isMember("component") ? json["component"].asString() : "";
    menu.status = json.isMember("status") ? json["status"].asString() : "";
    menu.permissionCode = json.isMember("permissionCode") ? json["permissionCode"].asString() : "";
    menu.isDefault = json.isMember("isDefault") ? json["isDefault"].asBool() : false;
    menu.createdAt = json.isMember("createdAt") ? json["createdAt"].asString() : "";
    menu.updatedAt = json.isMember("updatedAt") ? json["updatedAt"].asString() : "";
    return menu;
}

inline Json::Value menuRecordItemsToJson(const std::vector<MenuRecordSummary>& items) {
    return toJsonArray(items, [](const MenuRecordSummary& item) {
        return menuRecordToJson(item);
    });
}

inline std::vector<MenuRecordSummary> menuRecordItemsFromJson(const Json::Value& json) {
    return fromJsonArray<MenuRecordSummary>(json, [](const Json::Value& item) {
        return menuRecordFromJson(item);
    });
}

inline Json::Value menusToJson(const std::vector<MenuSummary>& menus) {
    return toJsonArray(menus, [](const MenuSummary& menu) {
        return menuToJson(menu);
    });
}

inline std::vector<std::string> permissionCodesFromMenus(const std::vector<MenuSummary>& menus) {
    std::vector<std::string> permissionCodes;
    std::unordered_set<std::string> seen;
    permissionCodes.reserve(menus.size());

    for (const auto& menu : menus) {
        if (menu.permissionCode.empty()) {
            continue;
        }

        if (!seen.insert(menu.permissionCode).second) {
            continue;
        }

        permissionCodes.push_back(menu.permissionCode);
    }

    return permissionCodes;
}

inline std::unordered_set<std::string> permissionCodeSetFromCodes(const std::vector<std::string>& permissionCodes) {
    std::unordered_set<std::string> permissionCodeSet;
    permissionCodeSet.reserve(permissionCodes.size());
    for (const auto& code : permissionCodes) {
        if (!code.empty()) {
            permissionCodeSet.insert(code);
        }
    }

    return permissionCodeSet;
}

inline std::unordered_set<std::string> permissionCodeSetFromMenus(const std::vector<MenuSummary>& menus) {
    std::unordered_set<std::string> permissionCodeSet;
    permissionCodeSet.reserve(menus.size());
    for (const auto& menu : menus) {
        if (!menu.permissionCode.empty()) {
            permissionCodeSet.insert(menu.permissionCode);
        }
    }

    return permissionCodeSet;
}

inline std::unordered_map<int, RoleSummary> enabledRoleSummaryMapFromRecords(
    const std::vector<RoleRecordSummary>& roleRecords) {
    std::unordered_map<int, RoleSummary> roleMap;
    roleMap.reserve(roleRecords.size());
    for (const auto& record : roleRecords) {
        if (record.status != "enabled") {
            continue;
        }

        roleMap.emplace(record.id, RoleSummary{record.id, record.name, record.code});
    }

    return roleMap;
}

inline std::vector<MenuSummary> enabledMenuSummariesFromRecords(
    const std::vector<MenuRecordSummary>& menuRecords) {
    std::vector<MenuSummary> menus;
    menus.reserve(menuRecords.size());
    for (const auto& record : menuRecords) {
        if (record.status != "enabled") {
            continue;
        }

        MenuSummary menu;
        menu.id = record.id;
        menu.name = record.name;
        menu.parentId = record.parentId;
        menu.type = record.type;
        menu.path = record.path;
        menu.component = record.component;
        menu.permissionCode = record.permissionCode;
        menu.icon = record.icon;
        menu.order = record.order;
        menu.visible = true;
        menus.push_back(std::move(menu));
    }
    return menus;
}

inline std::unordered_set<std::string> permissionCodeSetFromMenuRecords(
    const std::vector<MenuRecordSummary>& menuRecords,
    const std::unordered_set<int>& allowedMenuIds) {
    std::unordered_set<std::string> permissionCodeSet;
    permissionCodeSet.reserve(menuRecords.size());
    for (const auto& record : menuRecords) {
        if (record.status != "enabled") {
            continue;
        }

        if (allowedMenuIds.find(record.id) == allowedMenuIds.end()) {
            continue;
        }

        if (!record.permissionCode.empty()) {
            permissionCodeSet.insert(record.permissionCode);
        }
    }
    return permissionCodeSet;
}

inline Json::Value roleMenuToJson(const RoleMenuSummary& menu) {
    Json::Value json;
    json["id"] = menu.id;
    json["name"] = menu.name;
    json["type"] = menu.type;
    json["parentId"] = nullablePositiveIntToJson(menu.parentId);
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

inline RoleMenuSummary roleMenuSummaryFromMenuRecord(const MenuRecordSummary& menu) {
    RoleMenuSummary summary;
    summary.id = menu.id;
    summary.name = menu.name;
    summary.type = menu.type;
    summary.parentId = menu.parentId;
    return summary;
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

inline MenuSummary menuSummaryFromMenuRecord(const MenuRecordSummary& record) {
    MenuSummary menu;
    menu.id = record.id;
    menu.name = record.name;
    menu.parentId = record.parentId;
    menu.type = record.type;
    menu.path = record.path;
    menu.component = record.component;
    menu.permissionCode = record.permissionCode;
    menu.icon = record.icon;
    menu.order = record.order;
    menu.visible = record.status == "enabled";
    return menu;
}

inline Json::Value userRecordToJson(const UserRecordSummary& user) {
    Json::Value json;
    json["id"] = user.id;
    json["username"] = user.username;
    json["nickname"] = user.nickname;
    json["phone"] = user.phone;
    json["email"] = user.email;
    json["departmentId"] = nullablePositiveIntToJson(user.departmentId);
    json["departmentName"] = user.departmentName;
    json["status"] = user.status;
    json["createdAt"] = user.createdAt;
    json["updatedAt"] = user.updatedAt;
    return json;
}

inline UserRecordSummary userRecordFromJson(const Json::Value& json) {
    UserRecordSummary user;
    user.id = json.isMember("id") ? json["id"].asInt() : 0;
    user.username = json.isMember("username") ? json["username"].asString() : "";
    user.nickname = json.isMember("nickname") ? json["nickname"].asString() : "";
    user.phone = json.isMember("phone") ? json["phone"].asString() : "";
    user.email = json.isMember("email") ? json["email"].asString() : "";
    user.departmentId = json.isMember("departmentId") && !json["departmentId"].isNull()
        ? json["departmentId"].asInt()
        : 0;
    user.departmentName = json.isMember("departmentName") ? json["departmentName"].asString() : "";
    user.status = json.isMember("status") ? json["status"].asString() : "";
    user.createdAt = json.isMember("createdAt") ? json["createdAt"].asString() : "";
    user.updatedAt = json.isMember("updatedAt") ? json["updatedAt"].asString() : "";
    return user;
}

inline Json::Value userRecordItemsToJson(const std::vector<UserRecordSummary>& items) {
    return toJsonArray(items, [](const UserRecordSummary& item) {
        return userRecordToJson(item);
    });
}

inline std::vector<UserRecordSummary> userRecordItemsFromJson(const Json::Value& json) {
    return fromJsonArray<UserRecordSummary>(json, [](const Json::Value& item) {
        return userRecordFromJson(item);
    });
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
    json["parentId"] = nullablePositiveIntToJson(department.parentId);
    json["order"] = department.order;
    json["leaderId"] = nullablePositiveIntToJson(department.leaderId);
    json["status"] = department.status;
    json["createdAt"] = department.createdAt;
    json["updatedAt"] = department.updatedAt;
    return json;
}

inline DepartmentRecordSummary departmentRecordFromJson(const Json::Value& json) {
    DepartmentRecordSummary department;
    department.id = json.isMember("id") ? json["id"].asInt() : 0;
    department.name = json.isMember("name") ? json["name"].asString() : "";
    department.code = json.isMember("code") ? json["code"].asString() : "";
    department.parentId = json.isMember("parentId") ? json["parentId"].asInt() : 0;
    department.order = json.isMember("order") ? json["order"].asInt() : 0;
    department.leaderId = json.isMember("leaderId") ? json["leaderId"].asInt() : 0;
    department.status = json.isMember("status") ? json["status"].asString() : "";
    department.createdAt = json.isMember("createdAt") ? json["createdAt"].asString() : "";
    department.updatedAt = json.isMember("updatedAt") ? json["updatedAt"].asString() : "";
    return department;
}

inline Json::Value departmentRecordItemsToJson(const std::vector<DepartmentRecordSummary>& items) {
    return toJsonArray(items, [](const DepartmentRecordSummary& item) {
        return departmentRecordToJson(item);
    });
}

inline std::vector<DepartmentRecordSummary> departmentRecordItemsFromJson(const Json::Value& json) {
    return fromJsonArray<DepartmentRecordSummary>(json, [](const Json::Value& item) {
        return departmentRecordFromJson(item);
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
    json["permissionCodes"] = toJsonArray(user.permissionCodes, [](const std::string& code) {
        return Json::Value(code);
    });
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

inline void normalizeCurrentUser(CurrentUserSummary& user) {
    if (user.permissionCodes.empty() && !user.menus.empty()) {
        user.permissionCodes = permissionCodesFromMenus(user.menus);
    }
    if (user.permissionCodeSet.empty()) {
        if (!user.permissionCodes.empty()) {
            user.permissionCodeSet = permissionCodeSetFromCodes(user.permissionCodes);
        } else if (!user.menus.empty()) {
            user.permissionCodeSet = permissionCodeSetFromMenus(user.menus);
        }
    }
}

inline CurrentUserSummary currentUserFromJson(const Json::Value& json) {
    CurrentUserSummary user;
    user.id = json.isMember("id") ? json["id"].asInt() : 0;
    user.username = json.isMember("username") ? json["username"].asString() : "";
    user.nickname = json.isMember("nickname") ? json["nickname"].asString() : "";
    user.status = json.isMember("status") ? json["status"].asString() : "";
    user.roles = rolesFromJson(json.isMember("roles") ? json["roles"] : Json::Value(Json::arrayValue));
    user.menus = menusFromJson(json.isMember("menus") ? json["menus"] : Json::Value(Json::arrayValue));
    user.permissionCodes = fromJsonArray<std::string>(
        json.isMember("permissionCodes") ? json["permissionCodes"] : Json::Value(Json::arrayValue),
        [](const Json::Value& item) {
            return item.asString();
        });
    normalizeCurrentUser(user);
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
