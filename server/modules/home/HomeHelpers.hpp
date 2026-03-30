#pragma once

#include <cstdint>
#include <string>

#include <json/json.h>

namespace HomeHelpers {

struct HomeStatsSummary {
    int userCount = 0;
    int roleCount = 0;
    int menuCount = 0;
    int departmentCount = 0;
};

struct SystemInfoSummary {
    std::string version;
    std::string serverTime;
    int64_t uptime = 0;
    std::string platform;
};

inline Json::Value homeStatsToJson(const HomeStatsSummary& stats) {
    Json::Value json;
    json["userCount"] = stats.userCount;
    json["roleCount"] = stats.roleCount;
    json["menuCount"] = stats.menuCount;
    json["departmentCount"] = stats.departmentCount;
    return json;
}

inline Json::Value systemInfoToJson(const SystemInfoSummary& info) {
    Json::Value json;
    json["version"] = info.version;
    json["serverTime"] = info.serverTime;
    json["uptime"] = Json::Value::Int64(info.uptime);
    json["platform"] = info.platform;
    return json;
}

} // namespace HomeHelpers
