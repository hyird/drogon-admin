# 事务管理指南

## 问题

原有的事务使用方式存在以下问题：

```cpp
// ❌ 旧方式：手动管理事务
Task<void> update(int id, const Json::Value& data) {
    auto trans = co_await dbService_.newTransactionCoro();

    // 问题 1: 忘记提交会导致事务泄漏
    co_await trans->execSqlCoro("UPDATE ...");

    // 问题 2: 异常发生时没有回滚
    if (error) {
        throw std::runtime_error("error");  // 事务泄漏！
    }

    // 问题 3: 与缓存脱节，容易忘记清除
    // 缺少缓存清除逻辑
}
```

## 解决方案：TransactionGuard

`TransactionGuard` 是 RAII 风格的事务管理类，提供：

1. **自动回滚**：析构时自动回滚未提交的事务
2. **异常安全**：异常发生时保证事务回滚
3. **缓存集成**：提交成功后自动执行缓存清除
4. **明确的提交语义**：必须显式调用 `commit()`

## 基础用法

### 方式 1：手动管理（推荐用于复杂逻辑）

```cpp
#include "common/database/TransactionGuard.hpp"

Task<void> createUser(const Json::Value& data) {
    // 创建事务守卫
    auto tx = co_await TransactionGuard::create(dbService_);

    // 执行 SQL
    co_await tx.execSqlCoro("INSERT INTO sys_user (username, ...) VALUES (?, ...)", {
        data["username"].asString(),
        // ...
    });

    auto result = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
    int userId = F_INT(result[0]["id"]);

    co_await tx.execSqlCoro("INSERT INTO sys_user_role (userId, roleId) VALUES (?, ?)", {
        std::to_string(userId),
        std::to_string(roleId)
    });

    // 注册缓存清除回调（提交成功后执行）
    tx.onCommit([this, userId]() -> Task<void> {
        co_await cacheManager_.clearUserCache(userId);
    });

    // 提交事务（如果忘记提交，析构时自动回滚）
    co_await tx.commit();
}
```

### 方式 2：使用宏（推荐用于简单场景）

```cpp
Task<void> assignRoles(int userId, const Json::Value& roleIds) {
    TRANSACTION_DO(dbService_, [this, userId, &roleIds](auto& tx) -> Task<void> {
        // 删除旧角色
        co_await tx.execSqlCoro("DELETE FROM sys_user_role WHERE userId = ?", {
            std::to_string(userId)
        });

        // 插入新角色
        for (const auto& roleId : roleIds) {
            co_await tx.execSqlCoro("INSERT INTO sys_user_role (userId, roleId) VALUES (?, ?)", {
                std::to_string(userId),
                std::to_string(roleId.asInt())
            });
        }

        // 提交成功后清除缓存
        tx.onCommit([this, userId]() -> Task<void> {
            co_await cacheManager_.clearUserCache(userId);
        });
    });
    // 自动提交
}
```

## 高级用法

### 1. 条件提交

```cpp
Task<void> updateWithValidation(int id, const Json::Value& data) {
    auto tx = co_await TransactionGuard::create(dbService_);

    co_await tx.execSqlCoro("UPDATE sys_user SET name = ? WHERE id = ?", {
        data["name"].asString(),
        std::to_string(id)
    });

    // 验证逻辑
    auto result = co_await tx.execSqlCoro("SELECT COUNT(*) as cnt FROM sys_user WHERE name = ?", {
        data["name"].asString()
    });

    if (F_INT(result[0]["cnt"]) > 1) {
        // 显式回滚
        co_await tx.rollback();
        throw ValidationException("用户名重复");
    }

    // 验证通过，提交
    co_await tx.commit();
}
```

### 2. 多个缓存清除回调

```cpp
Task<void> updateUserAndRoles(int userId, const Json::Value& data) {
    auto tx = co_await TransactionGuard::create(dbService_);

    // 更新用户信息
    co_await tx.execSqlCoro("UPDATE sys_user SET ... WHERE id = ?", {...});

    // 更新角色关联
    co_await tx.execSqlCoro("DELETE FROM sys_user_role WHERE userId = ?", {...});
    co_await tx.execSqlCoro("INSERT INTO sys_user_role ...", {...});

    // 注册多个缓存清除回调（按顺序执行）
    tx.onCommit([this, userId]() -> Task<void> {
        co_await cacheManager_.deleteUserSession(userId);
    });

    tx.onCommit([this, userId]() -> Task<void> {
        co_await cacheManager_.deleteUserRoles(userId);
    });

    tx.onCommit([this, userId]() -> Task<void> {
        co_await cacheManager_.deleteUserMenus(userId);
    });

    co_await tx.commit();
}
```

### 3. 嵌套事务（使用保存点）

```cpp
Task<void> complexOperation(int userId) {
    auto tx = co_await TransactionGuard::create(dbService_);

    // 主操作
    co_await tx.execSqlCoro("UPDATE sys_user ...", {...});

    // 子操作（可能失败）
    try {
        co_await tx.execSqlCoroDirect("SAVEPOINT sp1");
        co_await tx.execSqlCoro("INSERT INTO sys_log ...", {...});
        co_await tx.execSqlCoroDirect("RELEASE SAVEPOINT sp1");
    } catch (const std::exception& e) {
        // 回滚到保存点
        co_await tx.execSqlCoroDirect("ROLLBACK TO SAVEPOINT sp1");
        LOG_WARN << "Sub-operation failed: " << e.what();
        // 继续主操作
    }

    co_await tx.commit();
}
```

### 4. 异常处理

```cpp
Task<void> safeUpdate(int id, const Json::Value& data) {
    auto tx = co_await TransactionGuard::create(dbService_);

    try {
        co_await tx.execSqlCoro("UPDATE sys_user ...", {...});

        // 可能抛出异常的业务逻辑
        validateData(data);

        co_await tx.commit();

    } catch (const ValidationException& e) {
        // 显式回滚
        co_await tx.rollback();
        LOG_WARN << "Validation failed, transaction rolled back: " << e.what();
        throw;

    } catch (const std::exception& e) {
        // 自动回滚（析构时）
        LOG_ERROR << "Update failed: " << e.what();
        throw;
    }
    // 如果没有 commit，析构时自动回滚
}
```

## 迁移指南

### 旧代码

```cpp
Task<void> create(const Json::Value& data) {
    auto trans = co_await dbService_.newTransactionCoro();

    std::string sql1 = "INSERT INTO sys_user ...";
    co_await trans->execSqlCoro(buildSql(sql1, params));

    auto lastIdResult = co_await trans->execSqlCoro("SELECT LAST_INSERT_ID() as id");
    int userId = F_INT(lastIdResult[0]["id"]);

    if (data.isMember("roleIds")) {
        std::string sql2 = "INSERT INTO sys_user_role ...";
        co_await trans->execSqlCoro(buildSql(sql2, params));
    }

    // 缺少缓存清除
    // 缺少显式提交
}
```

### 新代码

```cpp
Task<void> create(const Json::Value& data) {
    auto tx = co_await TransactionGuard::create(dbService_);

    co_await tx.execSqlCoro("INSERT INTO sys_user ...", params);

    auto lastIdResult = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
    int userId = F_INT(lastIdResult[0]["id"]);

    if (data.isMember("roleIds")) {
        co_await tx.execSqlCoro("INSERT INTO sys_user_role ...", params);
    }

    // 提交成功后清除缓存
    tx.onCommit([this, userId]() -> Task<void> {
        co_await cacheManager_.clearUserCache(userId);
    });

    co_await tx.commit();
}
```

## 最佳实践

### ✅ 推荐

```cpp
// 1. 使用 onCommit 确保缓存一致性
tx.onCommit([this, userId]() -> Task<void> {
    co_await cacheManager_.clearUserCache(userId);
});

// 2. 总是显式调用 commit()
co_await tx.commit();

// 3. 异常情况下让析构函数自动回滚
// 无需手动回滚（除非有特殊需求）

// 4. 简单场景使用宏
TRANSACTION_DO(dbService_, [this](auto& tx) -> Task<void> {
    co_await tx.execSqlCoro(...);
});
```

### ❌ 避免

```cpp
// 1. 不要忘记提交
auto tx = co_await TransactionGuard::create(dbService_);
co_await tx.execSqlCoro(...);
// 忘记 commit() - 事务会回滚！

// 2. 不要在缓存清除失败时回滚事务
tx.onCommit([this]() -> Task<void> {
    co_await cacheManager_.clearCache();  // 即使失败也不影响事务
});

// 3. 不要在事务外清除缓存
co_await tx.commit();
co_await cacheManager_.clearCache();  // ❌ 如果这里失败，缓存不一致

// 应该使用 onCommit
tx.onCommit([this]() -> Task<void> {
    co_await cacheManager_.clearCache();
});
co_await tx.commit();
```

## 性能考虑

1. **事务粒度**：保持事务尽可能小，避免长事务锁表
2. **批量操作**：多个插入合并为一个事务
3. **避免嵌套**：尽量避免嵌套事务，使用保存点代替
4. **缓存回调**：`onCommit` 回调在事务提交后异步执行，不影响事务性能

## 常见问题

### Q1: 什么时候需要显式回滚？

**A**: 通常不需要。析构时会自动回滚。只在以下情况显式回滚：
- 需要记录回滚日志
- 需要在回滚后继续执行其他逻辑
- 需要区分回滚原因

### Q2: onCommit 回调失败会怎样？

**A**: 事务已提交，回滚不可能。回调失败会记录日志，但不影响其他回调执行。

### Q3: 可以多次调用 commit() 吗？

**A**: 不可以。第二次调用会抛出异常。

### Q4: TransactionGuard 可以移动吗？

**A**: 可以。支持移动语义，移动后原对象不会回滚。

### Q5: 如何调试事务问题？

**A**: 开启 DEBUG 日志，会看到：
```
[DEBUG] Transaction committed successfully
[WARN] Transaction auto-rollback in destructor
```

## 示例：完整的用户创建流程

```cpp
Task<void> UserService::create(const Json::Value& data) {
    // 业务验证
    std::string username = data.get("username", "").asString();
    if (username.empty()) {
        throw ValidationException("用户名不能为空");
    }

    co_await checkUsernameUnique(username);

    // 开始事务
    auto tx = co_await TransactionGuard::create(dbService_);

    // 1. 插入用户
    std::string passwordHash = PasswordUtils::hashPassword(data.get("password", "").asString());
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

    // 2. 获取新用户 ID
    auto result = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
    int userId = F_INT(result[0]["id"]);

    // 3. 分配角色
    if (data.isMember("roleIds") && data["roleIds"].isArray()) {
        for (const auto& roleId : data["roleIds"]) {
            co_await tx.execSqlCoro(
                "INSERT INTO sys_user_role (userId, roleId) VALUES (?, ?)",
                {std::to_string(userId), std::to_string(roleId.asInt())}
            );
        }
    }

    // 4. 记录操作日志
    co_await tx.execSqlCoro(
        "INSERT INTO sys_operation_log (userId, action, createdAt) VALUES (?, ?, ?)",
        {std::to_string(userId), "create_user", TimestampHelper::now()}
    );

    // 5. 提交成功后清除相关缓存
    tx.onCommit([this, userId]() -> Task<void> {
        LOG_INFO << "User created, clearing cache for userId: " << userId;
        co_await cacheManager_.clearUserCache(userId);
        co_await cacheManager_.clearAllUserRolesCache();
    });

    // 6. 提交事务
    co_await tx.commit();

    LOG_INFO << "User created successfully: " << username;
}
```
