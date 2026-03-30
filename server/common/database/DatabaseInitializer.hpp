#pragma once

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include "common/utils/PasswordUtils.hpp"
#include "DatabaseService.hpp"
#include "modules/system/SystemConstants.hpp"

using namespace drogon;
using namespace drogon::orm;

class DatabaseInitializer {
private:
    static DbClientPtr getDbClient() {
        DatabaseService dbService;
        return dbService.getClient();
    }

public:
    static Task<> initialize() {
        auto db = getDbClient();
        if (!db) {
            LOG_ERROR << "Database client is not available, skip initialization";
            co_return;
        }

        LOG_INFO << "Checking database initialization...";

        // 检查表是否存在
        co_await createTables(db);

        // 检查是否有管理员用户
        co_await initializeAdminUser(db);

        LOG_INFO << "Database initialization completed";
    }

private:
    static Task<> createTables(const DbClientPtr& db) {
        // 创建用户表
        co_await db->execSqlCoro(R"(
            CREATE TABLE IF NOT EXISTS sys_user (
                id INT AUTO_INCREMENT PRIMARY KEY,
                username VARCHAR(50) NOT NULL UNIQUE,
                passwordHash VARCHAR(255) NOT NULL,
                nickname VARCHAR(100),
                email VARCHAR(100),
                phone VARCHAR(20),
                avatar VARCHAR(255),
                status ENUM('enabled', 'disabled') DEFAULT 'enabled',
                departmentId INT,
                createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                deletedAt TIMESTAMP NULL,
                INDEX idx_username (username),
                INDEX idx_deleted (deletedAt)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");

        // 创建角色表
        co_await db->execSqlCoro(R"(
            CREATE TABLE IF NOT EXISTS sys_role (
                id INT AUTO_INCREMENT PRIMARY KEY,
                name VARCHAR(50) NOT NULL,
                code VARCHAR(50) NOT NULL UNIQUE,
                description VARCHAR(255),
                status ENUM('enabled', 'disabled') DEFAULT 'enabled',
                `order` INT DEFAULT 0,
                createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                deletedAt TIMESTAMP NULL,
                INDEX idx_code (code),
                INDEX idx_deleted (deletedAt)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");

        // 创建菜单表
        co_await db->execSqlCoro(R"(
            CREATE TABLE IF NOT EXISTS sys_menu (
                id INT AUTO_INCREMENT PRIMARY KEY,
                name VARCHAR(50) NOT NULL,
                parentId INT DEFAULT 0,
                type ENUM('menu', 'page', 'button') DEFAULT 'page',
                path VARCHAR(255),
                component VARCHAR(255),
                permissionCode VARCHAR(100),
                icon VARCHAR(100),
                isDefault TINYINT(1) DEFAULT 0,
                status ENUM('enabled', 'disabled') DEFAULT 'enabled',
                `order` INT DEFAULT 0,
                createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                deletedAt TIMESTAMP NULL,
                INDEX idx_parent (parentId),
                INDEX idx_deleted (deletedAt)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");

        // 创建部门表
        co_await db->execSqlCoro(R"(
            CREATE TABLE IF NOT EXISTS sys_department (
                id INT AUTO_INCREMENT PRIMARY KEY,
                name VARCHAR(100) NOT NULL,
                parentId INT DEFAULT 0,
                leaderId INT,
                phone VARCHAR(20),
                email VARCHAR(100),
                status ENUM('enabled', 'disabled') DEFAULT 'enabled',
                `order` INT DEFAULT 0,
                createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                deletedAt TIMESTAMP NULL,
                INDEX idx_parent (parentId),
                INDEX idx_deleted (deletedAt)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");

        // 创建用户角色关联表
        co_await db->execSqlCoro(R"(
            CREATE TABLE IF NOT EXISTS sys_user_role (
                id INT AUTO_INCREMENT PRIMARY KEY,
                userId INT NOT NULL,
                roleId INT NOT NULL,
                createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uk_user_role (userId, roleId),
                INDEX idx_user (userId),
                INDEX idx_role (roleId),
                CONSTRAINT fk_user_role_user FOREIGN KEY (userId) REFERENCES sys_user(id) ON DELETE CASCADE,
                CONSTRAINT fk_user_role_role FOREIGN KEY (roleId) REFERENCES sys_role(id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");

        // 创建角色菜单关联表
        co_await db->execSqlCoro(R"(
            CREATE TABLE IF NOT EXISTS sys_role_menu (
                id INT AUTO_INCREMENT PRIMARY KEY,
                roleId INT NOT NULL,
                menuId INT NOT NULL,
                createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uk_role_menu (roleId, menuId),
                INDEX idx_role (roleId),
                INDEX idx_menu (menuId),
                CONSTRAINT fk_role_menu_role FOREIGN KEY (roleId) REFERENCES sys_role(id) ON DELETE CASCADE,
                CONSTRAINT fk_role_menu_menu FOREIGN KEY (menuId) REFERENCES sys_menu(id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )");

        LOG_INFO << "Database tables created/verified";
    }

    static Task<> initializeAdminUser(const DbClientPtr& db) {
        // 检查是否已有用户
        auto userCount = co_await db->execSqlCoro(
            "SELECT COUNT(*) as count FROM sys_user WHERE deletedAt IS NULL"
        );

        if (!userCount.empty() && userCount[0]["count"].as<int>() > 0) {
            LOG_INFO << "Users already exist, skipping initialization";
            co_return;
        }

        LOG_INFO << "No users found, creating default admin...";

        // 创建超级管理员角色
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_role (name, code, description, `order`) VALUES (?, ?, ?, ?)",
            {std::string(SystemConstants::DEFAULT_ADMIN_NICKNAME),
             std::string(SystemConstants::SUPERADMIN_ROLE_CODE),
             "拥有系统所有权限", "1"}
        ));

        // 获取角色ID
        auto roleResult = co_await db->execSqlCoro(
            buildSql("SELECT id FROM sys_role WHERE code = ?", {std::string(SystemConstants::SUPERADMIN_ROLE_CODE)})
        );
        int roleId = roleResult[0]["id"].as<int>();

        // 创建管理员用户 (密码: admin123)
        std::string passwordHash = PasswordUtils::hashPassword(std::string(SystemConstants::DEFAULT_ADMIN_PASSWORD));
        co_await db->execSqlCoro(buildSql(
            "INSERT INTO sys_user (username, passwordHash, nickname, status) VALUES (?, ?, ?, ?)",
            {std::string(SystemConstants::DEFAULT_ADMIN_USERNAME),
             passwordHash,
             std::string(SystemConstants::DEFAULT_ADMIN_NICKNAME),
             "enabled"}
        ));

        // 获取用户ID
        auto userResult = co_await db->execSqlCoro(
            buildSql("SELECT id FROM sys_user WHERE username = ?", {std::string(SystemConstants::DEFAULT_ADMIN_USERNAME)})
        );
        int userId = userResult[0]["id"].as<int>();

        // 分配角色
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_user_role (userId, roleId) VALUES (?, ?)",
            {std::to_string(userId), std::to_string(roleId)}
        ));

        // 创建基础菜单
        co_await initializeMenus(db);

        LOG_INFO << "Default admin user created: " << SystemConstants::DEFAULT_ADMIN_USERNAME << " / " << SystemConstants::DEFAULT_ADMIN_PASSWORD;
    }

    static Task<> initializeMenus(const DbClientPtr& db) {
        // 首页菜单 (作为独立一级菜单，默认显示)
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, isDefault, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            {"100", "首页", "0", "page", "/home", "Home", "HomeOutlined", "1", "0"}
        ));
        // 首页权限按钮
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"101", "查看统计", "100", "button", "home:dashboard:query", "1"}
        ));

        // 系统管理目录
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?)",
            {"1", "系统管理", "0", "menu", "/system", "SettingOutlined", "1"}
        ));

        // 用户管理页面
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {"2", "用户管理", "1", "page", "/system/user", "User", "UserOutlined", "1"}
        ));
        // 用户管理权限按钮
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"10", "查询用户", "2", "button", "system:user:query", "1"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"11", "新增用户", "2", "button", "system:user:add", "2"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"12", "编辑用户", "2", "button", "system:user:edit", "3"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"13", "删除用户", "2", "button", "system:user:delete", "4"}
        ));

        // 角色管理页面
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {"3", "角色管理", "1", "page", "/system/role", "Role", "TeamOutlined", "2"}
        ));
        // 角色管理权限按钮
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"20", "查询角色", "3", "button", "system:role:query", "1"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"21", "新增角色", "3", "button", "system:role:add", "2"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"22", "编辑角色", "3", "button", "system:role:edit", "3"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"23", "删除角色", "3", "button", "system:role:delete", "4"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"24", "分配权限", "3", "button", "system:role:perm", "5"}
        ));

        // 菜单管理页面
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {"4", "菜单管理", "1", "page", "/system/menu", "Menu", "MenuOutlined", "3"}
        ));
        // 菜单管理权限按钮
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"30", "查询菜单", "4", "button", "system:menu:query", "1"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"31", "新增菜单", "4", "button", "system:menu:add", "2"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"32", "编辑菜单", "4", "button", "system:menu:edit", "3"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"33", "删除菜单", "4", "button", "system:menu:delete", "4"}
        ));

        // 部门管理页面
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, path, component, icon, `order`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            {"5", "部门管理", "1", "page", "/system/department", "Dept", "ApartmentOutlined", "4"}
        ));
        // 部门管理权限按钮
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"40", "查询部门", "5", "button", "system:dept:query", "1"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"41", "新增部门", "5", "button", "system:dept:add", "2"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"42", "编辑部门", "5", "button", "system:dept:edit", "3"}
        ));
        co_await db->execSqlCoro(buildSql(
            "INSERT IGNORE INTO sys_menu (id, name, parentId, type, permissionCode, `order`) VALUES (?, ?, ?, ?, ?, ?)",
            {"43", "删除部门", "5", "button", "system:dept:delete", "4"}
        ));

        LOG_INFO << "Default menus created";
    }
};
