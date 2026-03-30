#pragma once

#include <drogon/drogon.h>
#include <stdexcept>

#include "common/database/DatabaseMigration.hpp"
#include "common/database/migrations/MigrationTransaction.hpp"
#include "common/utils/PasswordUtils.hpp"
#include "modules/system/SystemConstants.hpp"

namespace DatabaseMigrations {

inline drogon::Task<> applyV2SeedDefaultAdmin(const drogon::orm::DbClientPtr& /*db*/) {
    LOG_INFO << "Seeding default admin data...";

    DatabaseService dbService;
    co_await runTransactionalMigration(dbService, [](TransactionGuard& tx) -> Task<> {
        auto roleResult = co_await tx.execSqlCoro(
            buildSql("SELECT id FROM sys_role WHERE code = ?", {std::string(SystemConstants::SUPERADMIN_ROLE_CODE)})
        );

        if (roleResult.empty()) {
            co_await tx.execSqlCoro(buildSql(
                "INSERT IGNORE INTO sys_role (name, code, description, `order`) VALUES (?, ?, ?, ?)",
                {std::string(SystemConstants::DEFAULT_ADMIN_NICKNAME),
                 std::string(SystemConstants::SUPERADMIN_ROLE_CODE),
                 "拥有系统所有权限", "1"}
            ));

            roleResult = co_await tx.execSqlCoro(
                buildSql("SELECT id FROM sys_role WHERE code = ?", {std::string(SystemConstants::SUPERADMIN_ROLE_CODE)})
            );
        }

        if (roleResult.empty()) {
            throw std::runtime_error("Default admin role not found after seeding");
        }

        const int roleId = roleResult[0]["id"].as<int>();

        auto userResult = co_await tx.execSqlCoro(
            buildSql("SELECT id FROM sys_user WHERE username = ?", {std::string(SystemConstants::DEFAULT_ADMIN_USERNAME)})
        );

        if (userResult.empty()) {
            std::string passwordHash = PasswordUtils::hashPassword(std::string(SystemConstants::DEFAULT_ADMIN_PASSWORD));
            co_await tx.execSqlCoro(buildSql(
                "INSERT IGNORE INTO sys_user (username, passwordHash, nickname, status) VALUES (?, ?, ?, ?)",
                {std::string(SystemConstants::DEFAULT_ADMIN_USERNAME),
                 passwordHash,
                 std::string(SystemConstants::DEFAULT_ADMIN_NICKNAME),
                 "enabled"}
            ));

            userResult = co_await tx.execSqlCoro(
                buildSql("SELECT id FROM sys_user WHERE username = ?", {std::string(SystemConstants::DEFAULT_ADMIN_USERNAME)})
            );
        }

        if (userResult.empty()) {
            throw std::runtime_error("Default admin user not found after seeding");
        }

        const int userId = userResult[0]["id"].as<int>();

        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_user_role (userId, roleId) VALUES (?, ?)",
            {std::to_string(userId), std::to_string(roleId)}
        ));
    });

    LOG_INFO << "Default admin created/verified";
}

inline DatabaseMigration::Step createV2SeedDefaultAdminMigration() {
    return {2, "seed default admin", &applyV2SeedDefaultAdmin};
}

}  // namespace DatabaseMigrations
