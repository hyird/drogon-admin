#pragma once

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>

namespace DatabaseMigrationBootstrap {

inline drogon::Task<> ensureSchemaVersionTable(const drogon::orm::DbClientPtr& db) {
    co_await db->execSqlCoro(R"(
        CREATE TABLE IF NOT EXISTS sys_schema_migrations (
            version INT NOT NULL PRIMARY KEY,
            name VARCHAR(255) NOT NULL,
            appliedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    )");
}

}  // namespace DatabaseMigrationBootstrap
