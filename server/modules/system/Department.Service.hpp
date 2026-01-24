#pragma once

#include <drogon/drogon.h>
#include "common/database/DatabaseService.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/TreeBuilder.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"

using namespace drogon;

/**
 * @brief 部门服务类
 */
class DepartmentService {
private:
    DatabaseService dbService_;

public:
    Task<Json::Value> list(const std::string& keyword = "", const std::string& status = "") {
        QueryBuilder qb;
        qb.notDeleted();
        if (!keyword.empty()) qb.likeAny({"name", "code"}, keyword);
        if (!status.empty()) qb.eq("status", status);

        std::string sql = "SELECT * FROM sys_department" + qb.whereClause() + " ORDER BY `order` ASC, id ASC";
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
        std::string sql = "SELECT * FROM sys_department WHERE id = ? AND deletedAt IS NULL";
        auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(id)});
        if (result.empty()) throw NotFoundException("部门不存在");
        co_return rowToJson(result[0]);
    }

    Task<void> create(const Json::Value& data) {
        std::string code = data.get("code", "").asString();
        if (!code.empty()) co_await checkCodeUnique(code);

        std::string sql = R"(
            INSERT INTO sys_department (name, code, parentId, `order`, leaderId, status, createdAt)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        )";
        std::vector<std::string> params = {
            data.get("name", "").asString(), code,
            data.get("parentId", 0).isNull() ? "0" : std::to_string(data["parentId"].asInt()),
            std::to_string(data.get("order", 0).asInt()),
            data["leaderId"].isNull() ? "" : std::to_string(data["leaderId"].asInt()),
            data.get("status", "enabled").asString(),
            TimestampHelper::now()
        };
        co_await dbService_.execSqlCoro(sql, params);
    }

    Task<void> update(int id, const Json::Value& data) {
        co_await detail(id);
        if (data.isMember("code") && !data["code"].isNull()) {
            std::string code = data["code"].asString();
            if (!code.empty()) co_await checkCodeUnique(code, id);
        }

        std::vector<std::string> setClauses, params;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) { setClauses.push_back(f + " = ?"); params.push_back(v); };

        if (data.isMember("name")) addField("name", data["name"].asString());
        if (data.isMember("code")) addField("code", data["code"].asString());
        if (data.isMember("parentId")) addField("parentId", data["parentId"].isNull() ? "0" : std::to_string(data["parentId"].asInt()));
        if (data.isMember("order")) addField("`order`", std::to_string(data["order"].asInt()));
        if (data.isMember("leaderId")) addField("leaderId", data["leaderId"].isNull() ? "" : std::to_string(data["leaderId"].asInt()));
        if (data.isMember("status")) addField("status", data["status"].asString());

        if (setClauses.empty()) co_return;
        addField("updatedAt", TimestampHelper::now());
        params.push_back(std::to_string(id));

        std::string sql = "UPDATE sys_department SET ";
        for (size_t i = 0; i < setClauses.size(); ++i) { if (i > 0) sql += ", "; sql += setClauses[i]; }
        sql += " WHERE id = ?";
        co_await dbService_.execSqlCoro(sql, params);
    }

    Task<void> remove(int id) {
        auto checkSql = "SELECT COUNT(*) as count FROM sys_department WHERE parentId = ? AND deletedAt IS NULL";
        auto checkResult = co_await dbService_.execSqlCoro(checkSql, {std::to_string(id)});
        if (!checkResult.empty() && F_INT(checkResult[0]["count"]) > 0)
            throw ValidationException("该部门下存在子部门，无法删除");

        auto checkUserSql = "SELECT COUNT(*) as count FROM sys_user WHERE departmentId = ? AND deletedAt IS NULL";
        auto checkUserResult = co_await dbService_.execSqlCoro(checkUserSql, {std::to_string(id)});
        if (!checkUserResult.empty() && F_INT(checkUserResult[0]["count"]) > 0)
            throw ValidationException("该部门下存在用户，无法删除");

        co_await dbService_.execSqlCoro("UPDATE sys_department SET deletedAt = ? WHERE id = ?",
                                         {TimestampHelper::now(), std::to_string(id)});
    }

private:
    Task<void> checkCodeUnique(const std::string& code, int excludeId = 0) {
        std::string sql = "SELECT id FROM sys_department WHERE code = ? AND deletedAt IS NULL";
        std::vector<std::string> params = {code};
        if (excludeId > 0) { sql += " AND id != ?"; params.push_back(std::to_string(excludeId)); }
        auto result = co_await dbService_.execSqlCoro(sql, params);
        if (!result.empty()) throw ValidationException("部门编码已存在");
    }

    Json::Value rowToJson(const drogon::orm::Row& row) {
        Json::Value json;
        json["id"] = F_INT(row["id"]);
        json["name"] = F_STR(row["name"]);
        json["code"] = F_STR_DEF(row["code"], "");
        json["parentId"] = F_INT_DEF(row["parentId"], 0);
        json["order"] = F_INT_DEF(row["order"], 0);
        json["leaderId"] = F_INT_DEF(row["leaderId"], 0);
        json["status"] = F_STR_DEF(row["status"], "enabled");
        json["createdAt"] = F_STR_DEF(row["createdAt"], "");
        json["updatedAt"] = F_STR_DEF(row["updatedAt"], "");
        return json;
    }
};
