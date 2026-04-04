#pragma once

#include <drogon/drogon.h>
#include <optional>

#include "common/database/DatabaseMigration.hpp"
#include "common/database/migrations/MigrationTransaction.hpp"

namespace DatabaseMigrations {

inline drogon::Task<> applyV3SeedDefaultMenus(const drogon::orm::DbClientPtr& /*db*/) {
    DatabaseService dbService;
    co_await runTransactionalMigration(dbService, [](TransactionGuard& tx) -> Task<> {
        // 首页菜单（作为独立一级菜单，默认显示）
        co_await tx.execSqlCoro(buildSqlNullable(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, isDefault, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            std::vector<std::optional<std::string>>{"100", "首页", std::nullopt, "page", "/home", "Home", "HomeOutlined", "1", "0"}
        ));
        // 首页权限按钮
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"101", "查看统计", "100", "button", "home:dashboard:query", "1"}
        ));

        // 系统管理目录
        co_await tx.execSqlCoro(buildSqlNullable(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?)",
            std::vector<std::optional<std::string>>{"1", "系统管理", std::nullopt, "menu", "/system", "SettingOutlined", "1"}
        ));

        // 用户管理页面
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {"2", "用户管理", "1", "page", "/system/user", "User", "UserOutlined", "1"}
        ));
        // 用户管理权限按钮
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"10", "查询用户", "2", "button", "system:user:query", "1"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"11", "新增用户", "2", "button", "system:user:add", "2"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"12", "编辑用户", "2", "button", "system:user:edit", "3"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"13", "删除用户", "2", "button", "system:user:delete", "4"}
        ));

        // 角色管理页面
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {"3", "角色管理", "1", "page", "/system/role", "Role", "TeamOutlined", "2"}
        ));
        // 角色管理权限按钮
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"20", "查询角色", "3", "button", "system:role:query", "1"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"21", "新增角色", "3", "button", "system:role:add", "2"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"22", "编辑角色", "3", "button", "system:role:edit", "3"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"23", "删除角色", "3", "button", "system:role:delete", "4"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"24", "分配权限", "3", "button", "system:role:perm", "5"}
        ));

        // 菜单管理页面
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {"4", "菜单管理", "1", "page", "/system/menu", "Menu", "MenuOutlined", "3"}
        ));
        // 菜单管理权限按钮
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"30", "查询菜单", "4", "button", "system:menu:query", "1"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"31", "新增菜单", "4", "button", "system:menu:add", "2"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"32", "编辑菜单", "4", "button", "system:menu:edit", "3"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"33", "删除菜单", "4", "button", "system:menu:delete", "4"}
        ));

        // 部门管理页面
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {"5", "部门管理", "1", "page", "/system/department", "Dept", "ApartmentOutlined", "4"}
        ));
        // 部门管理权限按钮
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"40", "查询部门", "5", "button", "system:dept:query", "1"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"41", "新增部门", "5", "button", "system:dept:add", "2"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"42", "编辑部门", "5", "button", "system:dept:edit", "3"}
        ));
        co_await tx.execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"43", "删除部门", "5", "button", "system:dept:delete", "4"}
        ));
    });

    LOG_INFO << "Default menus created";
}

inline DatabaseMigration::Step createV3SeedDefaultMenusMigration() {
    return {3, "seed default menus", &applyV3SeedDefaultMenus};
}

}  // namespace DatabaseMigrations
