#pragma once

#include <drogon/drogon.h>

#include "common/database/DatabaseMigration.hpp"

namespace DatabaseMigrations {

inline drogon::Task<> applyV4NormalizeNullableReferences(const drogon::orm::DbClientPtr& db) {
    LOG_INFO << "Normalizing nullable reference columns to NULL...";

    co_await db->execSqlCoro("UPDATE sys_user SET departmentId = NULL WHERE departmentId = 0");
    co_await db->execSqlCoro("UPDATE sys_department SET parentId = NULL WHERE parentId = 0");
    co_await db->execSqlCoro("UPDATE sys_department SET leaderId = NULL WHERE leaderId = 0");
    co_await db->execSqlCoro("UPDATE sys_menu SET parentId = NULL WHERE parentId = 0");

    co_await db->execSqlCoro("ALTER TABLE sys_user MODIFY departmentId INT NULL DEFAULT NULL");
    co_await db->execSqlCoro("ALTER TABLE sys_department MODIFY parentId INT NULL DEFAULT NULL");
    co_await db->execSqlCoro("ALTER TABLE sys_department MODIFY leaderId INT NULL DEFAULT NULL");
    co_await db->execSqlCoro("ALTER TABLE sys_menu MODIFY parentId INT NULL DEFAULT NULL");

    LOG_INFO << "Nullable reference columns normalized";
}

inline DatabaseMigration::Step createV4NormalizeNullableReferencesMigration() {
    return {4, "normalize nullable references", &applyV4NormalizeNullableReferences};
}

}  // namespace DatabaseMigrations
