#pragma once

#include <drogon/drogon.h>

#include "SystemRelationLoader.hpp"
#include "SystemHelpers.hpp"

namespace AuthSessionBuilder {

inline Task<SystemHelpers::CurrentUserSummary> buildUserInfo(
    DatabaseService& dbService,
    CacheManager& cacheManager,
    int userId,
    const std::string& username,
    const std::string& nickname,
    const std::string& status) {
    SystemHelpers::CurrentUserSummary userInfo;
    userInfo.id = userId;
    userInfo.username = username;
    userInfo.nickname = nickname;
    userInfo.status = status;

    userInfo.roles = co_await SystemRelationLoader::loadUserRoles(dbService, cacheManager, userId);
    if (SystemHelpers::hasSuperadminRole(userInfo.roles)) {
        userInfo.menus = co_await SystemRelationLoader::loadAllMenus(dbService, cacheManager);
    } else {
        userInfo.menus = co_await SystemRelationLoader::loadUserMenus(dbService, cacheManager, userId, userInfo.roles);
    }
    userInfo.permissionCodes = SystemHelpers::permissionCodesFromMenus(userInfo.menus);

    co_return userInfo;
}

}  // namespace AuthSessionBuilder
