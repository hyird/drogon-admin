#pragma once

#include <string>

#include <drogon/drogon.h>

namespace DatabaseMigration {

struct Step {
    int version = 0;
    std::string name;
    drogon::Task<> (*apply)(const drogon::orm::DbClientPtr&);
};

}  // namespace DatabaseMigration
