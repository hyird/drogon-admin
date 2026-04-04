#pragma once

#include <optional>

#include <drogon/drogon.h>

#include "common/database/DatabaseMigration.hpp"
#include "common/database/migrations/MigrationTransaction.hpp"

namespace DatabaseMigrations {

inline drogon::Task<> applyV5AddDepartmentCodeAndSeedDepartments(const drogon::orm::DbClientPtr& /*db*/) {
    DatabaseService dbService;
    co_await runTransactionalMigration(dbService, [](TransactionGuard& tx) -> Task<> {
        auto columnResult = co_await tx.execSqlCoro(
            "SELECT COUNT(*) AS count FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ? AND COLUMN_NAME = ?",
            {"sys_department", "code"}
        );
        const bool hasCodeColumn = !columnResult.empty() && columnResult[0]["count"].as<int>() > 0;
        if (!hasCodeColumn) {
            co_await tx.execSqlCoro(
                "ALTER TABLE sys_department ADD COLUMN code VARCHAR(50) UNIQUE AFTER name"
            );
        }

        auto departmentCountResult = co_await tx.execSqlCoro(
            "SELECT COUNT(*) AS count FROM sys_department"
        );
        const bool hasDepartments = !departmentCountResult.empty()
                                   && departmentCountResult[0]["count"].as<int>() > 0;
        if (hasDepartments) {
            LOG_INFO << "Default departments already exist, skipping seed";
            co_return;
        }

        const auto seedDepartment = [&tx](int id,
                                          const std::string& name,
                                          std::optional<int> parentId,
                                          int order) -> Task<> {
            co_await tx.execSqlCoroNullable(
                "INSERT IGNORE INTO sys_department (id, name, parentId, code, `order`, status) "
                "VALUES (?, ?, ?, ?, ?, ?)",
                {
                    std::to_string(id),
                    name,
                    parentId.has_value()
                        ? std::optional<std::string>{std::to_string(*parentId)}
                        : std::nullopt,
                    std::nullopt,
                    std::to_string(order),
                    std::string("enabled"),
                }
            );
        };

        co_await seedDepartment(1, "总公司", std::nullopt, 1);
        co_await seedDepartment(2, "技术部", 1, 1);
        co_await seedDepartment(3, "产品部", 1, 2);
        co_await seedDepartment(4, "运营部", 1, 3);

        LOG_INFO << "Default departments seeded";
    });
}

inline DatabaseMigration::Step createV5AddDepartmentCodeAndSeedDepartmentsMigration() {
    return {5, "add department code and seed default departments", &applyV5AddDepartmentCodeAndSeedDepartments};
}

}  // namespace DatabaseMigrations
