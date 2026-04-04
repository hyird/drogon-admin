#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <drogon/drogon.h>

#include "common/cache/CacheManager.hpp"
#include "common/database/DatabaseService.hpp"
#include "common/utils/FieldHelper.hpp"
#include "SystemHelpers.hpp"
#include "SystemRecordLoader.hpp"

namespace AuthRecordLoader {

inline Task<std::optional<SystemHelpers::UserRecordSummary>> loadUserRecordById(
    DatabaseService& dbService,
    CacheManager& cacheManager,
    int userId) {
    co_return co_await SystemRecordLoader::loadCachedRecord<SystemHelpers::UserRecordSummary>(
        [&cacheManager]() -> Task<std::optional<std::vector<SystemHelpers::UserRecordSummary>>> {
            co_return co_await cacheManager.getUserRecords();
        },
        [userId](const std::vector<SystemHelpers::UserRecordSummary>& records) -> std::optional<SystemHelpers::UserRecordSummary> {
            auto it = std::find_if(records.begin(), records.end(), [userId](const auto& record) {
                return record.id == userId;
            });
            if (it == records.end()) {
                return std::nullopt;
            }
            return *it;
        },
        [&dbService, userId]() -> Task<std::optional<SystemHelpers::UserRecordSummary>> {
            std::string sql = R"(
                SELECT id, username, nickname, status
                FROM sys_user
                WHERE id = ? AND deletedAt IS NULL
            )";

            auto result = co_await dbService.execSqlCoro(sql, {std::to_string(userId)});
            if (result.empty()) {
                co_return std::nullopt;
            }

            SystemHelpers::UserRecordSummary user;
            auto row = result[0];
            user.id = F_INT(row["id"]);
            user.username = F_STR(row["username"]);
            user.nickname = F_STR_DEF(row["nickname"], "");
            user.status = F_STR(row["status"]);
            co_return user;
        }
    );
}

inline Task<std::optional<SystemHelpers::UserRecordSummary>> loadUserRecordByUsername(
    DatabaseService& dbService,
    CacheManager& cacheManager,
    const std::string& username) {
    co_return co_await SystemRecordLoader::loadCachedRecord<SystemHelpers::UserRecordSummary>(
        [&cacheManager]() -> Task<std::optional<std::vector<SystemHelpers::UserRecordSummary>>> {
            co_return co_await cacheManager.getUserRecords();
        },
        [&username](const std::vector<SystemHelpers::UserRecordSummary>& records) -> std::optional<SystemHelpers::UserRecordSummary> {
            auto it = std::find_if(records.begin(), records.end(), [&username](const auto& record) {
                return record.username == username;
            });
            if (it == records.end()) {
                return std::nullopt;
            }
            return *it;
        },
        [&dbService, username]() -> Task<std::optional<SystemHelpers::UserRecordSummary>> {
            std::string sql = R"(
                SELECT id, username, nickname, status
                FROM sys_user
                WHERE username = ? AND deletedAt IS NULL
            )";

            auto result = co_await dbService.execSqlCoro(sql, {username});
            if (result.empty()) {
                co_return std::nullopt;
            }

            SystemHelpers::UserRecordSummary user;
            auto row = result[0];
            user.id = F_INT(row["id"]);
            user.username = F_STR(row["username"]);
            user.nickname = F_STR_DEF(row["nickname"], "");
            user.status = F_STR(row["status"]);
            co_return user;
        }
    );
}

inline Task<std::optional<std::string>> loadUserPasswordHash(DatabaseService& dbService, int userId) {
    std::string sql = R"(
        SELECT passwordHash
        FROM sys_user
        WHERE id = ? AND deletedAt IS NULL
    )";

    auto result = co_await dbService.execSqlCoro(sql, {std::to_string(userId)});
    if (result.empty()) {
        co_return std::nullopt;
    }

    co_return F_STR(result[0]["passwordHash"]);
}

}  // namespace AuthRecordLoader
