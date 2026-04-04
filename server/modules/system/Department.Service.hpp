#pragma once

#include <algorithm>
#include <optional>

#include <drogon/drogon.h>
#include "common/database/DatabaseService.hpp"
#include "common/database/TransactionGuard.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/TreeBuilder.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"
#include "SystemHelpers.hpp"
#include "SystemEntityGuard.hpp"
#include "SystemDataLoader.hpp"
#include "SystemStringUtils.hpp"
#include "SystemRequests.hpp"

using namespace drogon;

/**
 * @brief 部门服务类
 */
class DepartmentService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    Task<std::vector<SystemHelpers::DepartmentRecordSummary>> list(const SystemRequests::DepartmentListQuery& query) {
        auto records = co_await SystemDataLoader::loadDepartmentRecords(dbService_, cacheManager_);
        co_return filterDepartmentRecords(records, query.status, query.keyword);
    }

    Task<std::vector<TreeBuilder::TreeNode<SystemHelpers::DepartmentRecordSummary>>> tree(const SystemRequests::DepartmentTreeQuery& query) {
        auto records = co_await SystemDataLoader::loadDepartmentRecords(dbService_, cacheManager_);
        auto items = filterDepartmentRecords(records, query.status, std::nullopt);

        auto tree = TreeBuilder::build(items,
            [](const auto& item) { return item.id; },
            [](const auto& item) { return item.parentId; });
        TreeBuilder::sort(tree, [](const auto& item) { return item.order; }, true);
        co_return tree;
    }

    Task<SystemHelpers::DepartmentRecordSummary> detail(int id) {
        auto records = co_await SystemDataLoader::loadDepartmentRecords(dbService_, cacheManager_);
        for (const auto& record : records) {
            if (record.id == id) {
                co_return record;
            }
        }

        throw NotFoundException("部门不存在");
    }

    Task<void> create(const SystemRequests::DepartmentCreateRequest& data) {
        auto tx = co_await TransactionGuard::create(dbService_);
        if (data.code && !data.code->empty()) {
            co_await SystemEntityGuard::ensureUniqueValue(tx, "sys_department", "code", *data.code, 0, "部门编码已存在");
        }
        if (data.parentId.has_value()) {
            co_await SystemEntityGuard::lockEnabledRowForUpdate(tx, "sys_department", *data.parentId, "父部门不存在或已禁用");
        }
        if (data.leaderId.has_value()) {
            co_await SystemEntityGuard::lockEnabledRowForUpdate(tx, "sys_user", *data.leaderId, "负责人不存在或已禁用");
        }

        std::string sql = R"(
            INSERT INTO sys_department (name, code, parentId, `order`, leaderId, status, createdAt)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        )";
        std::vector<std::optional<std::string>> params = {
            std::optional<std::string>{data.name},
            data.code.has_value()
                ? std::optional<std::string>{*data.code}
                : std::nullopt,
            data.parentId.has_value()
                ? std::optional<std::string>{std::to_string(*data.parentId)}
                : std::nullopt,
            std::optional<std::string>{std::to_string(data.order.value_or(0))},
            data.leaderId.has_value()
                ? std::optional<std::string>{std::to_string(*data.leaderId)}
                : std::nullopt,
            std::optional<std::string>{data.status},
            std::optional<std::string>{TimestampHelper::now()}
        };
        co_await tx.execSqlCoroNullable(sql, params);

        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateDepartmentCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateUserRecordsCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

    Task<void> update(int id, const SystemRequests::DepartmentUpdateRequest& data) {
        auto tx = co_await TransactionGuard::create(dbService_);
        co_await SystemEntityGuard::lockRowForUpdate(tx, "sys_department", id, "部门不存在");
        if (data.parentId.has_value() && data.parentId->has_value() && **data.parentId == id) {
            throw ValidationException("不能将部门设为自己的子部门");
        }
        if (data.code && !data.code->empty()) {
            co_await SystemEntityGuard::ensureUniqueValue(tx, "sys_department", "code", *data.code, id, "部门编码已存在");
        }

        if (data.parentId.has_value() && data.parentId->has_value()) {
            co_await SystemEntityGuard::lockEnabledRowForUpdate(tx, "sys_department", **data.parentId, "父部门不存在或已禁用");
        }
        if (data.leaderId.has_value() && data.leaderId->has_value()) {
            co_await SystemEntityGuard::lockEnabledRowForUpdate(tx, "sys_user", **data.leaderId, "负责人不存在或已禁用");
        }

        std::vector<std::optional<std::string>> params;
        std::vector<std::string> setClauses;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) {
            setClauses.push_back(f + " = ?");
            params.emplace_back(v);
        };

        if (data.name) addField("name", *data.name);
        if (data.code) addField("code", *data.code);
        if (data.parentId.has_value()) {
            setClauses.push_back("parentId = ?");
            if (data.parentId->has_value()) {
                params.emplace_back(std::to_string(**data.parentId));
            } else {
                params.emplace_back(std::nullopt);
            }
        }
        if (data.order) addField("`order`", std::to_string(*data.order));
        if (data.leaderId.has_value()) {
            setClauses.push_back("leaderId = ?");
            if (data.leaderId->has_value()) {
                params.emplace_back(std::to_string(**data.leaderId));
            } else {
                params.emplace_back(std::nullopt);
            }
        }
        if (data.status) addField("status", *data.status);

        if (setClauses.empty()) co_return;

        addField("updatedAt", TimestampHelper::now());
        params.push_back(std::to_string(id));

        std::string sql = "UPDATE sys_department SET ";
        for (size_t i = 0; i < setClauses.size(); ++i) { if (i > 0) sql += ", "; sql += setClauses[i]; }
        sql += " WHERE id = ?";
        co_await tx.execSqlCoroNullable(sql, params);

        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateDepartmentCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateUserRecordsCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

    Task<void> remove(int id) {
        auto tx = co_await TransactionGuard::create(dbService_);
        co_await SystemEntityGuard::lockRowForUpdate(tx, "sys_department", id, "部门不存在");

        if (co_await SystemEntityGuard::hasChildDepartment(tx, id)) {
            throw ValidationException("该部门下存在子部门，无法删除");
        }

        if (co_await SystemEntityGuard::hasAssignedDepartmentUsers(tx, id)) {
            throw ValidationException("该部门下存在用户，无法删除");
        }

        co_await tx.execSqlCoro("UPDATE sys_department SET deletedAt = ? WHERE id = ?",
                                 {TimestampHelper::now(), std::to_string(id)});

        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateDepartmentCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateUserRecordsCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

private:
    static std::vector<SystemHelpers::DepartmentRecordSummary> filterDepartmentRecords(
        const std::vector<SystemHelpers::DepartmentRecordSummary>& records,
        const std::optional<std::string>& status,
        const std::optional<std::string>& keyword) {
        std::vector<SystemHelpers::DepartmentRecordSummary> items;
        items.reserve(records.size());

        for (const auto& record : records) {
            if (status && record.status != *status) {
                continue;
            }

            if (keyword) {
                if (!SystemStringUtils::containsIgnoreCase(record.name, *keyword) &&
                    !SystemStringUtils::containsIgnoreCase(record.code, *keyword)) {
                    continue;
                }
            }

            items.push_back(record);
        }

        return items;
    }

};
