#pragma once

#include <exception>

#include "common/database/DatabaseService.hpp"
#include "common/database/TransactionGuard.hpp"

namespace DatabaseMigrations {

template <typename Work>
Task<> runTransactionalMigration(DatabaseService& dbService, Work work) {
    auto tx = co_await TransactionGuard::create(dbService);
    try {
        co_await work(tx);
        co_await tx.commit();
    } catch (const std::exception& e) {
        try {
            if (!tx.isRolledBack()) {
                tx.rollback();
            }
        } catch (const std::exception& rollbackError) {
            LOG_ERROR << "Failed to rollback transactional migration: " << rollbackError.what();
        }
        LOG_ERROR << "Transactional migration failed: " << e.what();
        throw;
    } catch (...) {
        try {
            if (!tx.isRolledBack()) {
                tx.rollback();
            }
        } catch (const std::exception& rollbackError) {
            LOG_ERROR << "Failed to rollback transactional migration: " << rollbackError.what();
        }
        LOG_ERROR << "Transactional migration failed with unknown exception";
        throw;
    }
}

}  // namespace DatabaseMigrations
