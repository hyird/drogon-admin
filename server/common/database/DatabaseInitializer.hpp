#pragma once

#include <algorithm>
#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <string>
#include <set>
#include <vector>

#include "DatabaseService.hpp"
#include "common/database/MigrationBootstrap.hpp"
#include "common/database/Migrations.hpp"

using namespace drogon;
using namespace drogon::orm;

class DatabaseInitializer {
private:
    static constexpr const char* MIGRATION_LOCK_NAME = "drogon_admin_schema_migration";

    static DbClientPtr getDbClient() {
        DatabaseService dbService;
        return dbService.getClient();
    }

    static Task<bool> acquireMigrationLock(const DbClientPtr& db) {
        auto result = co_await db->execSqlCoro(buildSql(
            "SELECT GET_LOCK(?, ?) AS locked",
            {MIGRATION_LOCK_NAME, "10"}
        ));

        if (result.empty() || result[0]["locked"].isNull()) {
            co_return false;
        }

        co_return result[0]["locked"].as<int>() > 0;
    }

    static Task<void> releaseMigrationLock(const DbClientPtr& db) {
        auto result = co_await db->execSqlCoro(buildSql(
            "SELECT RELEASE_LOCK(?) AS released",
            {MIGRATION_LOCK_NAME}
        ));

        if (!result.empty() && !result[0]["released"].isNull() && result[0]["released"].as<int>() == 1) {
            LOG_DEBUG << "Database migration lock released";
        } else {
            LOG_WARN << "Database migration lock release returned non-success status";
        }
    }

    static Task<std::vector<int>> getAppliedSchemaVersions(const DbClientPtr& db) {
        auto result = co_await db->execSqlCoro(
            "SELECT version FROM sys_schema_migrations ORDER BY version ASC"
        );

        std::vector<int> versions;
        versions.reserve(result.size());
        for (const auto& row : result) {
            versions.push_back(row["version"].as<int>());
        }

        co_return versions;
    }

    static Task<> recordMigration(const DbClientPtr& db, int version, const std::string& name) {
        co_await db->execSqlCoro(buildSql(
            "INSERT INTO sys_schema_migrations (version, name) VALUES (?, ?)",
            {std::to_string(version), name}
        ));
    }

    static std::string joinVersions(const std::vector<int>& versions) {
        if (versions.empty()) {
            return "none";
        }

        std::string output;
        for (size_t i = 0; i < versions.size(); ++i) {
            if (i > 0) {
                output += ", ";
            }
            output += std::to_string(versions[i]);
        }
        return output;
    }

    static int getLatestMigrationVersion(const std::vector<DatabaseMigration::Step>& steps) {
        int latestVersion = 0;
        for (const auto& step : steps) {
            latestVersion = std::max(latestVersion, step.version);
        }
        return latestVersion;
    }

    static bool validateMigrationDefinitions(const std::vector<DatabaseMigration::Step>& steps) {
        std::set<int> versions;
        bool valid = true;

        for (const auto& step : steps) {
            if (step.version <= 0) {
                LOG_ERROR << "Invalid migration version " << step.version
                          << " for step '" << step.name << "'";
                valid = false;
            }

            if (!versions.insert(step.version).second) {
                LOG_ERROR << "Duplicate migration version detected: " << step.version;
                valid = false;
            }
        }

        return valid;
    }

    static Task<bool> applyMigrations(const DbClientPtr& db) {
        bool lockAcquired = false;
        bool migrationSucceeded = false;

        try {
            co_await DatabaseMigrationBootstrap::ensureSchemaVersionTable(db);

            const auto& steps = migrations();
            if (!validateMigrationDefinitions(steps)) {
                LOG_ERROR << "Migration definitions are invalid, aborting database initialization";
                co_return false;
            }

            lockAcquired = co_await acquireMigrationLock(db);
            if (!lockAcquired) {
                LOG_ERROR << "Failed to acquire database migration lock, aborting initialization";
                co_return false;
            }

            const int latestVersion = getLatestMigrationVersion(steps);
            auto appliedVersions = co_await getAppliedSchemaVersions(db);
            std::set<int> appliedVersionSet(appliedVersions.begin(), appliedVersions.end());
            std::set<int> knownVersionSet;
            for (const auto& step : steps) {
                knownVersionSet.insert(step.version);
            }

            LOG_INFO << "Database schema versions recorded: " << joinVersions(appliedVersions);

            std::vector<int> unknownVersions;
            for (const auto& version : appliedVersions) {
                if (!knownVersionSet.contains(version)) {
                    unknownVersions.push_back(version);
                }
            }

            if (!unknownVersions.empty()) {
                LOG_WARN << "Database schema contains unknown versions: "
                         << joinVersions(unknownVersions);
            }

            std::vector<int> pendingVersions;
            pendingVersions.reserve(steps.size());
            for (const auto& step : steps) {
                if (!appliedVersionSet.contains(step.version)) {
                    pendingVersions.push_back(step.version);
                }
            }

            if (pendingVersions.empty()) {
                LOG_INFO << "Database schema is up to date";
            } else {
                LOG_INFO << "Pending database migrations: " << joinVersions(pendingVersions);
            }

            if (!appliedVersions.empty()) {
                const int highestApplied = *std::max_element(appliedVersions.begin(), appliedVersions.end());
                if (highestApplied > latestVersion) {
                    LOG_WARN << "Database schema version " << highestApplied
                             << " is ahead of latest known migration " << latestVersion;
                }
            }

            for (const auto& step : steps) {
                if (appliedVersionSet.contains(step.version)) {
                    continue;
                }

                LOG_INFO << "Applying database migration v" << step.version
                         << ": " << step.name;
                co_await step.apply(db);
                co_await recordMigration(db, step.version, step.name);
                appliedVersionSet.insert(step.version);
                LOG_INFO << "Database migration v" << step.version << " applied";
            }

            appliedVersions = co_await getAppliedSchemaVersions(db);
            appliedVersionSet = std::set<int>(appliedVersions.begin(), appliedVersions.end());

            std::vector<int> remainingVersions;
            remainingVersions.reserve(steps.size());
            for (const auto& step : steps) {
                if (!appliedVersionSet.contains(step.version)) {
                    remainingVersions.push_back(step.version);
                }
            }

            if (remainingVersions.empty()) {
                LOG_INFO << "Database schema is up to date";
                migrationSucceeded = true;
            } else {
                LOG_ERROR << "Database schema migration incomplete, missing versions: "
                          << joinVersions(remainingVersions);
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "Database migration failed: " << e.what();
            migrationSucceeded = false;
        }

        if (lockAcquired) {
            try {
                co_await releaseMigrationLock(db);
            } catch (const std::exception& e) {
                LOG_ERROR << "Failed to release database migration lock: " << e.what();
            }
        }

        co_return migrationSucceeded;
    }

public:
    static Task<bool> initialize() {
        auto db = getDbClient();
        if (!db) {
            LOG_ERROR << "Database client is not available, skip initialization";
            co_return false;
        }

        LOG_INFO << "Checking database initialization...";

        // 执行自动迁移并记录当前 schema 版本
        const bool migrationOk = co_await applyMigrations(db);
        if (!migrationOk) {
            LOG_ERROR << "Database migration did not complete, skip startup initialization";
            co_return false;
        }

        LOG_INFO << "Database initialization completed";
        co_return true;
    }

private:
    static const std::vector<DatabaseMigration::Step>& migrations() {
        return DatabaseMigrations::allMigrations();
    }
};
