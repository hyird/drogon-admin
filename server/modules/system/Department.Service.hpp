#pragma once

#include <drogon/drogon.h>
#include "common/database/DatabaseService.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/TreeBuilder.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"
#include "SystemHelpers.hpp"
#include "SystemRequests.hpp"

using namespace drogon;

/**
 * @brief 部门服务类
 */
class DepartmentService {
private:
    DatabaseService dbService_;

public:
    Task<std::vector<SystemHelpers::DepartmentRecordSummary>> list(const SystemRequests::DepartmentListQuery& query) {
        QueryBuilder qb;
        qb.notDeleted();
        if (query.keyword) {
            qb.likeAny({"name", "code"}, *query.keyword);
        }
        if (query.status) {
            qb.eq("status", *query.status);
        }

        std::string sql = "SELECT * FROM sys_department" + qb.whereClause() + " ORDER BY `order` ASC, id ASC";
        auto result = co_await dbService_.execSqlCoro(sql, qb.params());

        std::vector<SystemHelpers::DepartmentRecordSummary> items;
        items.reserve(result.size());
        for (const auto& row : result) {
            items.push_back(SystemHelpers::departmentRecordFromRow(row));
        }
        co_return items;
    }

    Task<std::vector<TreeBuilder::TreeNode<SystemHelpers::DepartmentRecordSummary>>> tree(const SystemRequests::DepartmentTreeQuery& query) {
        QueryBuilder qb;
        qb.notDeleted();
        if (query.status) {
            qb.eq("status", *query.status);
        }

        std::string sql = "SELECT * FROM sys_department" + qb.whereClause() + " ORDER BY `order` ASC, id ASC";
        auto result = co_await dbService_.execSqlCoro(sql, qb.params());

        std::vector<SystemHelpers::DepartmentRecordSummary> items;
        items.reserve(result.size());
        for (const auto& row : result) {
            items.push_back(SystemHelpers::departmentRecordFromRow(row));
        }

        auto tree = TreeBuilder::build(items,
            [](const auto& item) { return item.id; },
            [](const auto& item) { return item.parentId; });
        TreeBuilder::sort(tree, [](const auto& item) { return item.order; }, true);
        co_return tree;
    }

    Task<SystemHelpers::DepartmentRecordSummary> detail(int id) {
        std::string sql = "SELECT * FROM sys_department WHERE id = ? AND deletedAt IS NULL";
        auto result = co_await dbService_.execSqlCoro(sql, {std::to_string(id)});
        if (result.empty()) throw NotFoundException("部门不存在");
        co_return SystemHelpers::departmentRecordFromRow(result[0]);
    }

    Task<void> create(const SystemRequests::DepartmentCreateRequest& data) {
        if (data.code && !data.code->empty()) co_await checkCodeUnique(*data.code);

        std::string sql = R"(
            INSERT INTO sys_department (name, code, parentId, `order`, leaderId, status, createdAt)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        )";
        std::vector<std::string> params = {
            data.name,
            data.code.value_or(""),
            data.parentId.has_value() ? std::to_string(*data.parentId) : "0",
            std::to_string(data.order.value_or(0)),
            data.leaderId.has_value() ? std::to_string(*data.leaderId) : "",
            data.status,
            TimestampHelper::now()
        };
        co_await dbService_.execSqlCoro(sql, params);
    }

    Task<void> update(int id, const SystemRequests::DepartmentUpdateRequest& data) {
        co_await detail(id);
        if (data.code && !data.code->empty()) {
            co_await checkCodeUnique(*data.code, id);
        }

        std::vector<std::string> setClauses, params;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) { setClauses.push_back(f + " = ?"); params.push_back(v); };

        if (data.name) addField("name", *data.name);
        if (data.code) addField("code", *data.code);
        if (data.parentId.has_value()) {
            if (data.parentId->has_value()) {
                addField("parentId", std::to_string(**data.parentId));
            } else {
                addField("parentId", "0");
            }
        }
        if (data.order) addField("`order`", std::to_string(*data.order));
        if (data.leaderId.has_value()) {
            if (data.leaderId->has_value()) {
                addField("leaderId", std::to_string(**data.leaderId));
            } else {
                addField("leaderId", "");
            }
        }
        if (data.status) addField("status", *data.status);

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
    Task<void> checkCodeUnique(const std::string& code, int excludeId = 0) {
        std::string sql = "SELECT id FROM sys_department WHERE code = ? AND deletedAt IS NULL";
        std::vector<std::string> params = {code};
        if (excludeId > 0) { sql += " AND id != ?"; params.push_back(std::to_string(excludeId)); }
        auto result = co_await dbService_.execSqlCoro(sql, params);
        if (!result.empty()) throw ValidationException("部门编码已存在");
    }

};
