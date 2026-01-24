-- Admin Starter 数据库初始化脚本
-- MariaDB 11.x

CREATE DATABASE IF NOT EXISTS admin CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE admin;

-- 用户表
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
    INDEX idx_status (status),
    INDEX idx_deleted (deletedAt)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 角色表
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
    INDEX idx_status (status),
    INDEX idx_deleted (deletedAt)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 菜单/权限表
CREATE TABLE IF NOT EXISTS sys_menu (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(50) NOT NULL,
    parentId INT DEFAULT 0,
    type ENUM('directory', 'menu', 'button') DEFAULT 'menu',
    path VARCHAR(255),
    component VARCHAR(255),
    permissionCode VARCHAR(100),
    icon VARCHAR(100),
    status ENUM('enabled', 'disabled') DEFAULT 'enabled',
    `order` INT DEFAULT 0,
    createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deletedAt TIMESTAMP NULL,
    INDEX idx_parent (parentId),
    INDEX idx_permission (permissionCode),
    INDEX idx_status (status),
    INDEX idx_deleted (deletedAt)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 部门表
CREATE TABLE IF NOT EXISTS sys_department (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    code VARCHAR(50) UNIQUE,
    parentId INT DEFAULT 0,
    leaderId INT,
    status ENUM('enabled', 'disabled') DEFAULT 'enabled',
    `order` INT DEFAULT 0,
    createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deletedAt TIMESTAMP NULL,
    INDEX idx_code (code),
    INDEX idx_parent (parentId),
    INDEX idx_leader (leaderId),
    INDEX idx_status (status),
    INDEX idx_deleted (deletedAt)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 用户角色关联表
CREATE TABLE IF NOT EXISTS sys_user_role (
    id INT AUTO_INCREMENT PRIMARY KEY,
    userId INT NOT NULL,
    roleId INT NOT NULL,
    createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_user_role (userId, roleId),
    INDEX idx_user (userId),
    INDEX idx_role (roleId)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 角色菜单关联表
CREATE TABLE IF NOT EXISTS sys_role_menu (
    id INT AUTO_INCREMENT PRIMARY KEY,
    roleId INT NOT NULL,
    menuId INT NOT NULL,
    createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_role_menu (roleId, menuId),
    INDEX idx_role (roleId),
    INDEX idx_menu (menuId)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ========================================
-- 初始数据
-- ========================================

-- 超级管理员角色
INSERT INTO sys_role (name, code, description, `order`) VALUES
('超级管理员', 'superadmin', '拥有系统所有权限', 1),
('管理员', 'admin', '系统管理员', 2),
('普通用户', 'user', '普通用户', 3);

-- 初始用户 (密码: admin123)
-- PBKDF2-SHA256 hash of 'admin123' with salt 'adminsalt'
INSERT INTO sys_user (username, passwordHash, nickname, status) VALUES
('admin', 'pbkdf2:sha256:100000$adminsalt$e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855', '超级管理员', 'enabled');

-- 分配超级管理员角色
INSERT INTO sys_user_role (userId, roleId) VALUES (1, 1);

-- 系统菜单
INSERT INTO sys_menu (name, parentId, type, path, component, permissionCode, icon, `order`) VALUES
-- 系统管理目录
('系统管理', 0, 'directory', '/system', NULL, NULL, 'setting', 1),
-- 用户管理
('用户管理', 1, 'menu', '/system/user', 'system/user/index', 'system:user:list', 'user', 1),
('用户新增', 2, 'button', NULL, NULL, 'system:user:create', NULL, 1),
('用户编辑', 2, 'button', NULL, NULL, 'system:user:update', NULL, 2),
('用户删除', 2, 'button', NULL, NULL, 'system:user:delete', NULL, 3),
-- 角色管理
('角色管理', 1, 'menu', '/system/role', 'system/role/index', 'system:role:list', 'team', 2),
('角色新增', 6, 'button', NULL, NULL, 'system:role:create', NULL, 1),
('角色编辑', 6, 'button', NULL, NULL, 'system:role:update', NULL, 2),
('角色删除', 6, 'button', NULL, NULL, 'system:role:delete', NULL, 3),
-- 菜单管理
('菜单管理', 1, 'menu', '/system/menu', 'system/menu/index', 'system:menu:list', 'menu', 3),
('菜单新增', 10, 'button', NULL, NULL, 'system:menu:create', NULL, 1),
('菜单编辑', 10, 'button', NULL, NULL, 'system:menu:update', NULL, 2),
('菜单删除', 10, 'button', NULL, NULL, 'system:menu:delete', NULL, 3),
-- 部门管理
('部门管理', 1, 'menu', '/system/department', 'system/department/index', 'system:department:list', 'apartment', 4),
('部门新增', 14, 'button', NULL, NULL, 'system:department:create', NULL, 1),
('部门编辑', 14, 'button', NULL, NULL, 'system:department:update', NULL, 2),
('部门删除', 14, 'button', NULL, NULL, 'system:department:delete', NULL, 3);

-- 初始部门
INSERT INTO sys_department (name, parentId, `order`) VALUES
('总公司', 0, 1),
('技术部', 1, 1),
('产品部', 1, 2),
('运营部', 1, 3);

SELECT '数据库初始化完成!' AS message;
