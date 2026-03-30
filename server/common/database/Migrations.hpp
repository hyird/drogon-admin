#pragma once

#include <vector>

#include "common/database/DatabaseMigration.hpp"
#include "common/database/migrations/V1_CreateBaseSchema.hpp"
#include "common/database/migrations/V2_SeedDefaultAdmin.hpp"
#include "common/database/migrations/V3_SeedDefaultMenus.hpp"

namespace DatabaseMigrations {

inline const std::vector<DatabaseMigration::Step>& allMigrations() {
    static const std::vector<DatabaseMigration::Step> steps = {
        createV1CreateBaseSchemaMigration(),
        createV2SeedDefaultAdminMigration(),
        createV3SeedDefaultMenusMigration(),
    };
    return steps;
}

}  // namespace DatabaseMigrations
