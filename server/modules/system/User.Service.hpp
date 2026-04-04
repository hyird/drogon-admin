#pragma once

#include <algorithm>
#include <optional>

#include <drogon/drogon.h>
#include <unordered_map>

#include "common/database/DatabaseService.hpp"
#include "common/database/TransactionGuard.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/Pagination.hpp"
#include "common/utils/PasswordUtils.hpp"
#include "common/utils/AppException.hpp"
#include "common/utils/FieldHelper.hpp"
#include "common/utils/TimestampHelper.hpp"
#include "SystemHelpers.hpp"
#include "SystemConstants.hpp"
#include "SystemEntityGuard.hpp"
#include "SystemDataLoader.hpp"
#include "SystemRelationLoader.hpp"
#include "SystemStringUtils.hpp"
#include "SystemRequests.hpp"

using namespace drogon;

/**
 * @brief 用户服务类
 */
class UserService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    Task<std::tuple<std::vector<SystemHelpers::UserListItemSummary>, int>> list(const SystemRequests::UserListQuery& query) {
        auto records = co_await SystemDataLoader::loadUserRecords(dbService_, cacheManager_);
        auto filtered = filterUserRecords(records, query.status, query.departmentId, query.pagination.keyword);
        int total = static_cast<int>(filtered.size());

        auto paged = paginateUserRecords(filtered, query.pagination.page, query.pagination.pageSize);

        std::vector<SystemHelpers::UserListItemSummary> items;
        items.reserve(paged.size());
        std::vector<int> userIds;
        userIds.reserve(paged.size());
        for (const auto& record : paged) {
            SystemHelpers::UserListItemSummary item;
            item.user = record;
            userIds.push_back(item.user.id);
            items.push_back(std::move(item));
        }

        auto cachedRoles = co_await cacheManager_.getUserRolesBatch(userIds);
        std::vector<bool> cacheHits(items.size(), false);
        std::vector<int> missingUserIds;
        missingUserIds.reserve(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            if (i < cachedRoles.size() && cachedRoles[i]) {
                items[i].roles = *cachedRoles[i];
                cacheHits[i] = true;
            } else {
                missingUserIds.push_back(items[i].user.id);
            }
        }

        std::unordered_map<int, std::vector<SystemHelpers::RoleSummary>> loadedRoles;
        if (!missingUserIds.empty()) {
            loadedRoles = co_await SystemRelationLoader::loadUserRolesByIds(dbService_, cacheManager_, missingUserIds);
        }

        for (size_t i = 0; i < items.size(); ++i) {
            if (cacheHits[i]) {
                continue;
            }

            auto it = loadedRoles.find(items[i].user.id);
            if (it != loadedRoles.end()) {
                items[i].roles = it->second;
            } else {
                items[i].roles.clear();
            }

            co_await cacheManager_.cacheUserRoles(items[i].user.id, items[i].roles);
        }

        co_return std::make_tuple(items, total);
    }

    Task<SystemHelpers::UserDetailSummary> detail(int id) {
        auto records = co_await SystemDataLoader::loadUserRecords(dbService_, cacheManager_);
        SystemHelpers::UserDetailSummary item;
        bool found = false;
        for (const auto& record : records) {
            if (record.id == id) {
                item.user = record;
                found = true;
                break;
            }
        }
        if (!found) throw NotFoundException("用户不存在");
        item.roles = co_await SystemRelationLoader::loadUserRoles(dbService_, cacheManager_, id);
        item.roleIds = co_await SystemDataLoader::loadUserRoleIds(dbService_, id);
        co_return item;
    }

    Task<void> create(const SystemRequests::UserCreateRequest& data) {
        const std::string& username = data.username;

        auto tx = co_await TransactionGuard::create(dbService_);
        co_await SystemEntityGuard::ensureUniqueValue(tx, "sys_user", "username", username, 0, "用户名已存在");
        if (data.departmentId.has_value()) {
            co_await SystemEntityGuard::lockEnabledRowForUpdate(tx, "sys_department", *data.departmentId, "部门不存在或已禁用");
        }
        std::string passwordHash = PasswordUtils::hashPassword(data.password);

        std::vector<std::optional<std::string>> params = {
            std::optional<std::string>{username},
            std::optional<std::string>{passwordHash},
            std::optional<std::string>{data.nickname.value_or("")},
            std::optional<std::string>{data.phone.value_or("")},
            std::optional<std::string>{data.email.value_or("")},
            data.departmentId.has_value()
                ? std::optional<std::string>{std::to_string(*data.departmentId)}
                : std::nullopt,
            std::optional<std::string>{data.status},
            std::optional<std::string>{TimestampHelper::now()},
        };
        co_await tx.execSqlCoroNullable(
            "INSERT INTO sys_user (username, passwordHash, nickname, phone, email, departmentId, status, createdAt) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            params
        );

        auto lastIdResult = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
        int userId = F_INT(lastIdResult[0]["id"]);

        if (!data.roleIds.empty()) {
            co_await syncRoles(tx, userId, data.roleIds);
        }

        // 提交成功后清除缓存
        tx.onCommit([this, userId]() -> Task<void> {
            co_await cacheManager_.clearUserCache(userId);
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateUserRecordsCache();
        });
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.invalidateHomeCache();
        });

        co_await tx.commit();
    }

    Task<void> update(int id, const SystemRequests::UserUpdateRequest& data) {
        auto tx = co_await TransactionGuard::create(dbService_);
        auto user = co_await SystemEntityGuard::lockUserRecordForUpdate(tx, id);
        if (user.username == SystemConstants::DEFAULT_ADMIN_USERNAME) {
            throw ForbiddenException("不能编辑或删除内建管理员账户");
        }

        if (data.departmentId.has_value() && data.departmentId->has_value()) {
            co_await SystemEntityGuard::lockEnabledRowForUpdate(tx, "sys_department", **data.departmentId, "部门不存在或已禁用");
        }

        std::vector<std::optional<std::string>> params;
        std::vector<std::string> setClauses;
        auto addField = [&setClauses, &params](const std::string& f, const std::string& v) {
            setClauses.push_back(f + " = ?");
            params.emplace_back(v);
        };

        if (data.nickname) addField("nickname", *data.nickname);
        if (data.phone) addField("phone", *data.phone);
        if (data.email) addField("email", *data.email);
        if (data.departmentId.has_value()) {
            setClauses.push_back("departmentId = ?");
            if (data.departmentId->has_value()) {
                params.emplace_back(std::to_string(**data.departmentId));
            } else {
                params.emplace_back(std::nullopt);
            }
        }
        if (data.status) addField("status", *data.status);
        if (data.password) addField("passwordHash", PasswordUtils::hashPassword(*data.password));

        if (!setClauses.empty()) {
            addField("updatedAt", TimestampHelper::now());
            params.push_back(std::to_string(id));
            std::string sql = "UPDATE sys_user SET ";
            for (size_t i = 0; i < setClauses.size(); ++i) { if (i > 0) sql += ", "; sql += setClauses[i]; }
            sql += " WHERE id = ?";
            co_await tx.execSqlCoroNullable(sql, params);
        }

        if (data.roleIds) {
            co_await syncRoles(tx, id, *data.roleIds);
        }

        // 提交成功后清除用户缓存
        tx.onCommit([this, id]() -> Task<void> {
            co_await cacheManager_.clearUserCache(id);
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
        auto user = co_await SystemEntityGuard::lockUserRecordForUpdate(tx, id);
        if (user.username == SystemConstants::DEFAULT_ADMIN_USERNAME) {
            throw ForbiddenException("不能编辑或删除内建管理员账户");
        }

        co_await tx.execSqlCoro("DELETE FROM sys_user_role WHERE userId = ?", {std::to_string(id)});
        co_await tx.execSqlCoro("UPDATE sys_user SET deletedAt = ? WHERE id = ?", {TimestampHelper::now(), std::to_string(id)});

        // 提交成功后清除用户缓存
        tx.onCommit([this, id]() -> Task<void> {
            co_await cacheManager_.clearUserCache(id);
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
    static std::vector<SystemHelpers::UserRecordSummary> filterUserRecords(
        const std::vector<SystemHelpers::UserRecordSummary>& records,
        const std::optional<std::string>& status,
        const std::optional<int>& departmentId,
        const std::string& keyword) {
        std::vector<SystemHelpers::UserRecordSummary> items;
        items.reserve(records.size());

        for (const auto& record : records) {
            if (status && record.status != *status) {
                continue;
            }

            if (departmentId && record.departmentId != *departmentId) {
                continue;
            }

            if (!keyword.empty() &&
                !SystemStringUtils::containsIgnoreCase(record.username, keyword) &&
                !SystemStringUtils::containsIgnoreCase(record.nickname, keyword) &&
                !SystemStringUtils::containsIgnoreCase(record.phone, keyword) &&
                !SystemStringUtils::containsIgnoreCase(record.email, keyword)) {
                continue;
            }

            items.push_back(record);
        }

        return items;
    }

    static std::vector<SystemHelpers::UserRecordSummary> paginateUserRecords(
        const std::vector<SystemHelpers::UserRecordSummary>& records,
        int page,
        int pageSize) {
        if (pageSize <= 0) {
            return records;
        }

        const long long safePage = std::max(page, 1);
        const long long offset = static_cast<long long>(safePage - 1) * pageSize;
        if (offset >= static_cast<long long>(records.size())) {
            return {};
        }

        const auto begin = records.begin() + static_cast<std::vector<SystemHelpers::UserRecordSummary>::difference_type>(offset);
        const auto end = records.begin() + static_cast<std::vector<SystemHelpers::UserRecordSummary>::difference_type>(
            std::min<long long>(offset + pageSize, static_cast<long long>(records.size())));
        return std::vector<SystemHelpers::UserRecordSummary>(begin, end);
    }

    Task<void> syncRoles(TransactionGuard& tx, int userId, const std::vector<int>& roleIds) {
        co_await SystemEntityGuard::ensureEnabledIds(tx, "sys_role", roleIds, "角色不存在或已禁用");
        co_await tx.execSqlCoro("DELETE FROM sys_user_role WHERE userId = ?", {std::to_string(userId)});

        std::vector<int> uniqueRoleIds = roleIds;
        std::sort(uniqueRoleIds.begin(), uniqueRoleIds.end());
        uniqueRoleIds.erase(std::unique(uniqueRoleIds.begin(), uniqueRoleIds.end()), uniqueRoleIds.end());

        if (!uniqueRoleIds.empty()) {
            std::string sql = "INSERT INTO sys_user_role (userId, roleId) VALUES ";
            std::vector<std::string> valueParts, params;
            for (int roleId : uniqueRoleIds) {
                valueParts.push_back("(?, ?)");
                params.push_back(std::to_string(userId));
                params.push_back(std::to_string(roleId));
            }
            sql += valueParts[0];
            for (size_t i = 1; i < valueParts.size(); ++i) sql += ", " + valueParts[i];
            co_await tx.execSqlCoro(sql, params);
        }
    }

};
