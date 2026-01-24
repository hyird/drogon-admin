#pragma once

#include <drogon/drogon.h>
#include "common/database/DatabaseService.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/TreeBuilder.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"

using namespace drogon;

/**
 * @brief 菜单服务类
 */
class MenuService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    Task<Json::Value> list(const std::string& keyword = "", const std::string& status = "") {
        QueryBuilder qb;
        qb.notDeleted();
        if (!keyword.empty()) qb.likeAny({"name", "path", "permissionCode"}, keyword);
        if (!status.empty()) qb.eq("status", status);

        std::string sql = "SELECT * FROM sys_menu" + qb.whereClause() + " ORDER BY `order` ASC, id ASC";
        auto result = co_await dbService_.execSqlCoro(sql, qb.params());

        Json::Value items(Json::arrayValue);
        for (const auto& row : result) items.append(rowToJson(row));
        co_return items;
    }

    Task<Json::Value> tree(const std::string& status = "") {
        auto items = co_await list("", status);
        auto tree = TreeBuilder::build(items);
        TreeBuilder::sort(tree, "order", true);
        co_return tree;
    }

    Task<Json::Value> detail(int id) {
        std::string sql = "SELECT * FROM sys_menu WHERE id = ? AND deletedAt IS NULL";
        auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(id)});
        if (result.empty()) throw NotFoundException("菜单不存在");
        co_return rowToJson(result[0]);
    }

    Task<void> create(const Json::Value& data) {
        std::string sql = R"(
            INSERT INTO sys_menu (name, path, icon, parentId, `order`, type, component, status, permissionCode, isDefault, createdAt)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )";
        std::vector<std::string> params = {
            data.get("name", "").asString(), data.get("path", "").asString(), data.get("icon", "").asString(),
            data.get("parentId", 0).isNull() ? "0" : std::to_string(data["parentId"].asInt()),
            std::to_string(data.get("order", 0).asInt()), data.get("type", "menu").asString(),
            data.get("component", "").asString(), data.get("status", "enabled").asString(),
            data.get("permissionCode", "").asString(), data.get("isDefault", false).asBool() ? "1" : "0",
            TimestampHelper::now()
        };
        co_await dbService_.execSqlCoro(sql, params);

        // 清除所有用户的菜单缓存
        co_await cacheManager_.clearAllUserMenusCache();
    }

    Task<void> update(int id, const Json::Value& data) {
        co_await detail(id);
        if (data.isMember("parentId") && !data["parentId"].isNull() && data["parentId"].asInt() == id)
            throw ValidationException("不能将菜单设为自己的子菜单");

        std::vector<std::string> setClauses, params;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) { setClauses.push_back(f + " = ?"); params.push_back(v); };

        if (data.isMember("name")) addField("name", data["name"].asString());
        if (data.isMember("path")) addField("path", data["path"].asString());
        if (data.isMember("icon")) addField("icon", data["icon"].asString());
        if (data.isMember("parentId")) addField("parentId", data["parentId"].isNull() ? "0" : std::to_string(data["parentId"].asInt()));
        if (data.isMember("order")) addField("`order`", std::to_string(data["order"].asInt()));
        if (data.isMember("type")) addField("type", data["type"].asString());
        if (data.isMember("component")) addField("component", data["component"].asString());
        if (data.isMember("status")) addField("status", data["status"].asString());
        if (data.isMember("permissionCode")) addField("permissionCode", data["permissionCode"].asString());
        if (data.isMember("isDefault")) addField("isDefault", data["isDefault"].asBool() ? "1" : "0");

        if (setClauses.empty()) co_return;
        addField("updatedAt", TimestampHelper::now());
        params.push_back(std::to_string(id));

        std::string sql = "UPDATE sys_menu SET ";
        for (size_t i = 0; i < setClauses.size(); ++i) { if (i > 0) sql += ", "; sql += setClauses[i]; }
        sql += " WHERE id = ?";
        co_await dbService_.execSqlCoro(sql, params);

        // 清除所有用户的菜单缓存
        co_await cacheManager_.clearAllUserMenusCache();
    }

    Task<void> remove(int id) {
        auto checkSql = "SELECT COUNT(*) as count FROM sys_menu WHERE parentId = ? AND deletedAt IS NULL";
        auto checkResult = co_await dbService_.execSqlCoro(checkSql, {std::to_string(id)});
        if (!checkResult.empty() && F_INT(checkResult[0]["count"]) > 0)
            throw ValidationException("该菜单下存在子菜单，无法删除");

        co_await dbService_.execSqlCoro("DELETE FROM sys_role_menu WHERE menuId = ?", {std::to_string(id)});
        co_await dbService_.execSqlCoro("UPDATE sys_menu SET deletedAt = ? WHERE id = ?",
                                         {TimestampHelper::now(), std::to_string(id)});

        // 清除所有用户的菜单缓存
        co_await cacheManager_.clearAllUserMenusCache();
    }

private:
    Json::Value rowToJson(const drogon::orm::Row& row) {
        Json::Value json;
        json["id"] = F_INT(row["id"]);
        json["name"] = F_STR(row["name"]);
        json["path"] = F_STR_DEF(row["path"], "");
        json["icon"] = F_STR_DEF(row["icon"], "");
        json["parentId"] = F_INT_DEF(row["parentId"], 0);
        json["order"] = F_INT_DEF(row["order"], 0);
        json["type"] = F_STR_DEF(row["type"], "menu");
        json["component"] = F_STR_DEF(row["component"], "");
        json["status"] = F_STR_DEF(row["status"], "enabled");
        json["permissionCode"] = F_STR_DEF(row["permissionCode"], "");
        json["isDefault"] = F_INT_DEF(row["isDefault"], 0) == 1;
        json["createdAt"] = F_STR_DEF(row["createdAt"], "");
        json["updatedAt"] = F_STR_DEF(row["updatedAt"], "");
        return json;
    }
};
