# TransactionGuard 实际使用示例

## User.Service.hpp 改造示例

### 原代码（存在问题）

```cpp
Task<void> create(const Json::Value& data) {
    std::string username = data.get("username", "").asString();
    co_await checkUsernameUnique(username);

    auto trans = co_await dbService_.newTransactionCoro();  // ❌ 手动管理
    std::string passwordHash = PasswordUtils::hashPassword(data.get("password", "").asString());

    std::string sql = "INSERT INTO sys_user (username, passwordHash, ...) VALUES (?, ?, ...)";
    std::vector<std::string> params = { username, passwordHash, ... };
    co_await trans->execSqlCoro(buildSql(sql, params));  // ❌ 手动拼接 SQL

    auto lastIdResult = co_await trans->execSqlCoro("SELECT LAST_INSERT_ID() as id");
    int userId = F_INT(lastIdResult[0]["id"]);

    if (data.isMember("roleIds") && data["roleIds"].isArray())
        co_await syncRoles(trans, userId, data["roleIds"]);  // ❌ 传递裸指针

    // ❌ 缺少提交
    // ❌ 缺少缓存清除
    // ❌ 异常不安全
}
```

### 新代码（推荐）

```cpp
#include "common/database/TransactionGuard.hpp"

Task<void> create(const Json::Value& data) {
    std::string username = data.get("username", "").asString();
    co_await checkUsernameUnique(username);

    // ✅ RAII 风格，自动回滚
    auto tx = co_await TransactionGuard::create(dbService_);

    std::string passwordHash = PasswordUtils::hashPassword(data.get("password", "").asString());

    // ✅ 自动参数替换和转义
    co_await tx.execSqlCoro(
        "INSERT INTO sys_user (username, passwordHash, nickname, phone, email, departmentId, status, createdAt) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        {
            username,
            passwordHash,
            data.get("nickname", "").asString(),
            data.get("phone", "").asString(),
            data.get("email", "").asString(),
            data.get("departmentId", 0).isNull() ? "0" : std::to_string(data["departmentId"].asInt()),
            data.get("status", "enabled").asString(),
            TimestampHelper::now()
        }
    );

    auto lastIdResult = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
    int userId = F_INT(lastIdResult[0]["id"]);

    // ✅ 传递引用而非裸指针
    if (data.isMember("roleIds") && data["roleIds"].isArray()) {
        co_await syncRoles(tx, userId, data["roleIds"]);
    }

    // ✅ 提交成功后自动清除缓存
    tx.onCommit([this, userId]() -> Task<void> {
        co_await cacheManager_.clearUserCache(userId);
    });

    // ✅ 显式提交
    co_await tx.commit();
}

// 辅助方法也需要修改
Task<void> syncRoles(TransactionGuard& tx, int userId, const Json::Value& roleIds) {
    // 删除旧角色
    co_await tx.execSqlCoro(
        "DELETE FROM sys_user_role WHERE userId = ?",
        {std::to_string(userId)}
    );

    // 插入新角色
    for (const auto& roleId : roleIds) {
        co_await tx.execSqlCoro(
            "INSERT INTO sys_user_role (userId, roleId) VALUES (?, ?)",
            {std::to_string(userId), std::to_string(roleId.asInt())}
        );
    }
}
```

## Role.Service.hpp 改造示例

### 更新角色权限

```cpp
Task<void> updatePermissions(int roleId, const Json::Value& menuIds) {
    auto tx = co_await TransactionGuard::create(dbService_);

    // 删除旧权限
    co_await tx.execSqlCoro(
        "DELETE FROM sys_role_menu WHERE roleId = ?",
        {std::to_string(roleId)}
    );

    // 插入新权限
    for (const auto& menuId : menuIds) {
        co_await tx.execSqlCoro(
            "INSERT INTO sys_role_menu (roleId, menuId) VALUES (?, ?)",
            {std::to_string(roleId), std::to_string(menuId.asInt())}
        );
    }

    // 提交成功后清除所有用户的权限缓存
    tx.onCommit([this]() -> Task<void> {
        co_await cacheManager_.clearAllUserRolesCache();
        co_await cacheManager_.clearAllUserMenusCache();
        LOG_INFO << "Cleared all user permissions cache after role update";
    });

    co_await tx.commit();
}
```

### 批量删除角色

```cpp
Task<void> batchRemove(const std::vector<int>& roleIds) {
    auto tx = co_await TransactionGuard::create(dbService_);

    for (int roleId : roleIds) {
        // 检查是否为内置角色
        auto result = co_await tx.execSqlCoro(
            "SELECT isBuiltin FROM sys_role WHERE id = ?",
            {std::to_string(roleId)}
        );

        if (!result.empty() && F_BOOL(result[0]["isBuiltin"])) {
            throw ValidationException("不能删除内置角色");
        }

        // 删除角色关联
        co_await tx.execSqlCoro(
            "DELETE FROM sys_user_role WHERE roleId = ?",
            {std::to_string(roleId)}
        );

        co_await tx.execSqlCoro(
            "DELETE FROM sys_role_menu WHERE roleId = ?",
            {std::to_string(roleId)}
        );

        // 软删除角色
        co_await tx.execSqlCoro(
            "UPDATE sys_role SET deletedAt = ? WHERE id = ?",
            {TimestampHelper::now(), std::to_string(roleId)}
        );
    }

    // 批量清除缓存
    tx.onCommit([this]() -> Task<void> {
        co_await cacheManager_.clearAllUserRolesCache();
        co_await cacheManager_.clearAllUserMenusCache();
    });

    co_await tx.commit();
}
```

## Menu.Service.hpp 改造示例

### 移动菜单（修改父级）

```cpp
Task<void> move(int menuId, int newParentId) {
    // 检查是否会形成循环引用
    co_await checkCircularReference(menuId, newParentId);

    auto tx = co_await TransactionGuard::create(dbService_);

    // 更新父级
    co_await tx.execSqlCoro(
        "UPDATE sys_menu SET parentId = ?, updatedAt = ? WHERE id = ?",
        {std::to_string(newParentId), TimestampHelper::now(), std::to_string(menuId)}
    );

    // 重新计算排序（可选）
    co_await recalculateOrder(tx, newParentId);

    // 提交后清除所有菜单缓存
    tx.onCommit([this]() -> Task<void> {
        co_await cacheManager_.clearAllUserMenusCache();
    });

    co_await tx.commit();
}

Task<void> recalculateOrder(TransactionGuard& tx, int parentId) {
    auto menus = co_await tx.execSqlCoro(
        "SELECT id FROM sys_menu WHERE parentId = ? AND deletedAt IS NULL ORDER BY `order`",
        {std::to_string(parentId)}
    );

    int order = 1;
    for (const auto& menu : menus) {
        co_await tx.execSqlCoro(
            "UPDATE sys_menu SET `order` = ? WHERE id = ?",
            {std::to_string(order++), std::to_string(F_INT(menu["id"]))}
        );
    }
}
```

## Department.Service.hpp 改造示例

### 移动部门（影响子部门）

```cpp
Task<void> move(int deptId, int newParentId) {
    co_await checkCircularReference(deptId, newParentId);

    auto tx = co_await TransactionGuard::create(dbService_);

    // 更新部门父级
    co_await tx.execSqlCoro(
        "UPDATE sys_department SET parentId = ?, updatedAt = ? WHERE id = ?",
        {std::to_string(newParentId), TimestampHelper::now(), std::to_string(deptId)}
    );

    // 更新所有子部门的路径（如果使用路径字段）
    auto children = co_await getDescendants(tx, deptId);
    for (const auto& child : children) {
        std::string newPath = calculatePath(child.id, newParentId);
        co_await tx.execSqlCoro(
            "UPDATE sys_department SET path = ? WHERE id = ?",
            {newPath, std::to_string(child.id)}
        );
    }

    // 提交后清除部门缓存
    tx.onCommit([this]() -> Task<void> {
        co_await cacheManager_.clearDepartmentCache();
    });

    co_await tx.commit();
}
```

## 使用宏的简化示例

### 简单的角色分配

```cpp
Task<void> assignRole(int userId, int roleId) {
    TRANSACTION_DO(dbService_, [this, userId, roleId](auto& tx) -> Task<void> {
        // 检查用户是否已有该角色
        auto result = co_await tx.execSqlCoro(
            "SELECT COUNT(*) as cnt FROM sys_user_role WHERE userId = ? AND roleId = ?",
            {std::to_string(userId), std::to_string(roleId)}
        );

        if (F_INT(result[0]["cnt"]) > 0) {
            throw ValidationException("用户已拥有该角色");
        }

        // 插入角色关联
        co_await tx.execSqlCoro(
            "INSERT INTO sys_user_role (userId, roleId) VALUES (?, ?)",
            {std::to_string(userId), std::to_string(roleId)}
        );

        // 清除缓存
        tx.onCommit([this, userId]() -> Task<void> {
            co_await cacheManager_.clearUserCache(userId);
        });
    });
}
```

### 批量操作

```cpp
Task<void> batchUpdateStatus(const std::vector<int>& userIds, const std::string& status) {
    TRANSACTION_DO(dbService_, [this, &userIds, &status](auto& tx) -> Task<void> {
        for (int userId : userIds) {
            co_await tx.execSqlCoro(
                "UPDATE sys_user SET status = ?, updatedAt = ? WHERE id = ?",
                {status, TimestampHelper::now(), std::to_string(userId)}
            );
        }

        // 批量清除缓存
        tx.onCommit([this, &userIds]() -> Task<void> {
            for (int userId : userIds) {
                co_await cacheManager_.clearUserCache(userId);
            }
        });
    });
}
```

## 错误处理示例

### 带验证的更新

```cpp
Task<void> updateWithValidation(int id, const Json::Value& data) {
    auto tx = co_await TransactionGuard::create(dbService_);

    try {
        // 检查是否存在
        auto result = co_await tx.execSqlCoro(
            "SELECT * FROM sys_user WHERE id = ? AND deletedAt IS NULL",
            {std::to_string(id)}
        );

        if (result.empty()) {
            throw NotFoundException("用户不存在");
        }

        // 检查用户名唯一性
        if (data.isMember("username")) {
            auto checkResult = co_await tx.execSqlCoro(
                "SELECT COUNT(*) as cnt FROM sys_user WHERE username = ? AND id != ?",
                {data["username"].asString(), std::to_string(id)}
            );

            if (F_INT(checkResult[0]["cnt"]) > 0) {
                throw ValidationException("用户名已存在");
            }
        }

        // 更新数据
        co_await tx.execSqlCoro(
            "UPDATE sys_user SET username = ?, updatedAt = ? WHERE id = ?",
            {data["username"].asString(), TimestampHelper::now(), std::to_string(id)}
        );

        // 提交并清除缓存
        tx.onCommit([this, id]() -> Task<void> {
            co_await cacheManager_.clearUserCache(id);
        });

        co_await tx.commit();

    } catch (const ValidationException& e) {
        // 验证失败，显式回滚
        co_await tx.rollback();
        LOG_WARN << "Validation failed: " << e.what();
        throw;
    }
    // 其他异常会自动回滚（析构时）
}
```

### 部分失败继续执行

```cpp
Task<void> importUsers(const Json::Value& users) {
    int successCount = 0;
    int failureCount = 0;

    for (const auto& user : users) {
        try {
            // 每个用户单独开启事务
            auto tx = co_await TransactionGuard::create(dbService_);

            co_await tx.execSqlCoro(
                "INSERT INTO sys_user (username, ...) VALUES (?, ...)",
                {user["username"].asString(), ...}
            );

            co_await tx.commit();
            successCount++;

        } catch (const std::exception& e) {
            LOG_ERROR << "Failed to import user: " << user["username"].asString()
                      << ", error: " << e.what();
            failureCount++;
            // 继续下一个用户
        }
    }

    LOG_INFO << "Import completed: " << successCount << " success, "
             << failureCount << " failed";
}
```

## 性能优化示例

### 批量插入优化

```cpp
// ❌ 低效：每条记录一个事务
Task<void> batchInsertSlow(const std::vector<User>& users) {
    for (const auto& user : users) {
        auto tx = co_await TransactionGuard::create(dbService_);
        co_await tx.execSqlCoro("INSERT INTO sys_user ...", {...});
        co_await tx.commit();
    }
}

// ✅ 高效：一个事务批量插入
Task<void> batchInsertFast(const std::vector<User>& users) {
    auto tx = co_await TransactionGuard::create(dbService_);

    for (const auto& user : users) {
        co_await tx.execSqlCoro("INSERT INTO sys_user ...", {...});
    }

    tx.onCommit([this]() -> Task<void> {
        co_await cacheManager_.clearAllUserCache();
    });

    co_await tx.commit();
}

// ✅ 更高效：使用批量 INSERT
Task<void> batchInsertFastest(const std::vector<User>& users) {
    if (users.empty()) return;

    auto tx = co_await TransactionGuard::create(dbService_);

    // 构建批量 INSERT 语句
    std::string sql = "INSERT INTO sys_user (username, nickname) VALUES ";
    std::vector<std::string> params;

    for (size_t i = 0; i < users.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += "(?, ?)";
        params.push_back(users[i].username);
        params.push_back(users[i].nickname);
    }

    co_await tx.execSqlCoro(sql, params);

    tx.onCommit([this]() -> Task<void> {
        co_await cacheManager_.clearAllUserCache();
    });

    co_await tx.commit();
}
```

## 总结

| 场景 | 推荐方式 | 原因 |
|------|---------|------|
| 简单单表操作 | `TRANSACTION_DO` 宏 | 代码简洁 |
| 复杂多表操作 | 手动 `TransactionGuard` | 更灵活的控制 |
| 需要条件提交 | 手动 + 显式 `rollback()` | 明确回滚逻辑 |
| 批量操作 | 单个事务批量执行 | 性能最优 |
| 需要缓存一致性 | `onCommit()` 回调 | 保证原子性 |
