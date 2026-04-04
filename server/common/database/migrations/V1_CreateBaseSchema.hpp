#pragma once

#include <drogon/drogon.h>

#include "common/database/DatabaseMigration.hpp"

namespace DatabaseMigrations {

inline drogon::Task<> applyV1CreateBaseSchema(const drogon::orm::DbClientPtr& db) {
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
            departmentId INT NULL DEFAULT NULL,
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
            parentId INT NULL DEFAULT NULL,
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
            code VARCHAR(50) UNIQUE,
            parentId INT NULL DEFAULT NULL,
            leaderId INT NULL DEFAULT NULL,
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

    LOG_INFO << "Base database schema created/verified";
}

inline DatabaseMigration::Step createV1CreateBaseSchemaMigration() {
    return {1, "create base tables", &applyV1CreateBaseSchema};
}

}  // namespace DatabaseMigrations
