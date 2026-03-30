# 数据库与缓存最佳实践

## 架构概览

```
┌─────────────────────────────────────────────────────────────┐
│                        Controller 层                         │
│  - 参数验证                                                    │
│  - 权限检查                                                    │
│  - 调用 Service                                               │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                        Service 层                            │
│  - 业务逻辑                                                   │
│  - 使用 TransactionGuard 管理事务                             │
│  - 使用 CacheManager 管理缓存                                 │
└───────────┬──────────────────────────┬──────────────────────┘
            │                          │
            ▼                          ▼
┌──────────────────────┐   ┌──────────────────────┐
│  TransactionGuard    │   │   CacheManager       │
│  - RAII 事务管理      │   │  - 业务缓存封装       │
│  - 自动回滚          │   │  - 统一 TTL 管理      │
│  - 缓存集成          │   │  - 缓存失效策略       │
└──────────┬───────────┘   └──────────┬───────────┘
           │                          │
           ▼                          ▼
┌──────────────────────┐   ┌──────────────────────┐
│  DatabaseService     │   │   RedisService       │
│  - SQL 参数转义      │   │  - 协程 API          │
│  - 协程查询          │   │  - JSON 序列化       │
└──────────┬───────────┘   └──────────┬───────────┘
           │                          │
           ▼                          ▼
┌──────────────────────┐   ┌──────────────────────┐
│  Drogon ORM          │   │  Drogon Redis        │
│  - MySQL 连接池       │   │  - Redis 连接池       │
└──────────────────────┘   └──────────────────────┘
```

## 核心原则

### 1. 事务与缓存一致性

**原则：事务提交成功后才清除缓存**

```cpp
// ✅ 正确：使用 onCommit 保证一致性
auto tx = co_await TransactionGuard::create(dbService_);
co_await tx.execSqlCoro("UPDATE sys_user ...", {...});

tx.onCommit([this, userId]() -> Task<void> {
    co_await cacheManager_.clearUserCache(userId);
});

co_await tx.commit();  // 先提交事务，再清缓存

// ❌ 错误：事务可能回滚但缓存已清除
co_await cacheManager_.clearUserCache(userId);  // 缓存先清了
co_await tx.commit();  // 如果这里失败，数据不一致
```

### 2. 缓存降级

**原则：Redis 不可用时自动降级到数据库**

```cpp
Task<Json::Value> getUserInfo(int userId) {
    // 先尝试缓存
    auto cached = co_await cacheManager_.getUserSession(userId);
    if (cached) {
        co_return *cached;
    }

    // 缓存未命中或不可用，查询数据库
    auto userInfo = co_await queryDatabase(userId);

    // 尝试写入缓存（失败也不影响）
    co_await cacheManager_.cacheUserSession(userId, userInfo);

    co_return userInfo;
}
```

### 3. 事务粒度

**原则：保持事务尽可能小，避免长事务**

```cpp
// ✅ 好：事务只包含必要的数据库操作
Task<void> updateUser(int id, const Json::Value& data) {
    // 1. 事务外：业务验证（可能耗时）
    co_await validateUserData(data);

    // 2. 事务内：快速执行数据库操作
    auto tx = co_await TransactionGuard::create(dbService_);
    co_await tx.execSqlCoro("UPDATE sys_user ...", {...});
    co_await tx.commit();

    // 3. 事务外：发送通知（可能耗时）
    co_await sendNotification(id);
}

// ❌ 坏：事务包含耗时操作
Task<void> updateUserBad(int id, const Json::Value& data) {
    auto tx = co_await TransactionGuard::create(dbService_);

    co_await validateUserData(data);  // ❌ 验证在事务内
    co_await tx.execSqlCoro("UPDATE sys_user ...", {...});
    co_await sendNotification(id);    // ❌ 通知在事务内

    co_await tx.commit();
}
```

### 4. 缓存键命名规范

**原则：统一的命名空间，便于批量清除**

```cpp
// ✅ 好：有层级结构
"session:user:123"           // 用户会话
"user:roles:123"             // 用户角色
"user:menus:123"             // 用户菜单
"menu:all"                   // 全局菜单
"login:failed:admin"         // 登录失败计数
"ratelimit:123:api:users"    // API 限流

// ❌ 坏：无规律
"u123"
"roles_123"
"menus-123"
```

## 典型场景实现

### 场景 1：创建用户（多表操作 + 缓存）

```cpp
Task<void> UserService::create(const Json::Value& data) {
    // 1. 事务外：业务验证
    std::string username = data["username"].asString();
    co_await checkUsernameUnique(username);

    // 2. 开启事务
    auto tx = co_await TransactionGuard::create(dbService_);

    // 3. 插入用户
    co_await tx.execSqlCoro(
        "INSERT INTO sys_user (username, passwordHash, ...) VALUES (?, ?, ...)",
        {username, passwordHash, ...}
    );

    auto result = co_await tx.execSqlCoro("SELECT LAST_INSERT_ID() as id");
    int userId = F_INT(result[0]["id"]);

    // 4. 分配角色
    for (const auto& roleId : data["roleIds"]) {
        co_await tx.execSqlCoro(
            "INSERT INTO sys_user_role (userId, roleId) VALUES (?, ?)",
            {std::to_string(userId), std::to_string(roleId.asInt())}
        );
    }

    // 5. 提交成功后清除缓存
    tx.onCommit([this, userId]() -> Task<void> {
        co_await cacheManager_.clearUserCache(userId);
        co_await cacheManager_.clearAllUserRolesCache();
    });

    // 6. 提交
    co_await tx.commit();
}
```

### 场景 2：更新角色权限（影响所有用户）

```cpp
Task<void> RoleService::updatePermissions(int roleId, const Json::Value& menuIds) {
    auto tx = co_await TransactionGuard::create(dbService_);

    // 删除旧权限
    co_await tx.execSqlCoro("DELETE FROM sys_role_menu WHERE roleId = ?", {std::to_string(roleId)});

    // 插入新权限
    for (const auto& menuId : menuIds) {
        co_await tx.execSqlCoro(
            "INSERT INTO sys_role_menu (roleId, menuId) VALUES (?, ?)",
            {std::to_string(roleId), std::to_string(menuId.asInt())}
        );
    }

    // 清除所有用户的权限缓存
    tx.onCommit([this]() -> Task<void> {
        co_await cacheManager_.clearAllUserRolesCache();
        co_await cacheManager_.clearAllUserMenusCache();
        LOG_INFO << "Cleared all user permissions cache";
    });

    co_await tx.commit();
}
```

### 场景 3：登录（限流 + 缓存）

```cpp
Task<HttpResponsePtr> AuthController::login(HttpRequestPtr req) {
    auto json = req->getJsonObject();
    std::string username = (*json)["username"].asString();

    // 1. 检查登录失败次数（Redis）
    auto failureCount = co_await cacheManager_.getLoginFailureCount(username);
    if (failureCount >= 5) {
        co_return Response::error(429, "登录失败次数过多，请15分钟后再试", 429);
    }

    // 2. 验证用户名密码（Database）
    auto user = co_await queryUser(username);
    if (!user || !verifyPassword(password, user.passwordHash)) {
        // 记录失败（Redis）
        co_await cacheManager_.recordLoginFailure(username);
        throw AuthException::PasswordIncorrect();
    }

    // 3. 登录成功，清除失败记录（Redis）
    co_await cacheManager_.clearLoginFailure(username);

    // 4. 生成 Token
    std::string token = jwtUtils_->sign({{"userId", user.id}});

    // 5. 构建用户信息（先查缓存，再查数据库）
    auto userInfo = co_await buildUserInfo(user.id);

    // 6. 缓存用户会话（Redis）
    co_await cacheManager_.cacheUserSession(user.id, userInfo);

    // 7. 返回结果
    co_return Response::ok({{"token", token}, {"user", userInfo}});
}
```

### 场景 4：批量操作（性能优化）

```cpp
Task<void> UserService::batchUpdateStatus(const std::vector<int>& userIds, const std::string& status) {
    // 使用单个事务处理批量操作
    auto tx = co_await TransactionGuard::create(dbService_);

    // 方式 1: 循环更新
    for (int userId : userIds) {
        co_await tx.execSqlCoro(
            "UPDATE sys_user SET status = ? WHERE id = ?",
            {status, std::to_string(userId)}
        );
    }

    // 方式 2: 批量 UPDATE（更高效）
    // UPDATE sys_user SET status = 'disabled' WHERE id IN (1, 2, 3, ...)
    std::string sql = "UPDATE sys_user SET status = ? WHERE id IN (";
    std::vector<std::string> params = {status};

    for (size_t i = 0; i < userIds.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += "?";
        params.push_back(std::to_string(userIds[i]));
    }
    sql += ")";

    co_await tx.execSqlCoro(sql, params);

    // 批量清除缓存
    tx.onCommit([this, &userIds]() -> Task<void> {
        for (int userId : userIds) {
            co_await cacheManager_.clearUserCache(userId);
        }
    });

    co_await tx.commit();
}
```

## 性能优化建议

### 1. 减少数据库查询

```cpp
// ❌ N+1 查询问题
Task<Json::Value> getUserListBad() {
    auto users = co_await dbService_.execSqlCoro("SELECT * FROM sys_user");

    Json::Value result(Json::arrayValue);
    for (const auto& user : users) {
        int userId = F_INT(user["id"]);

        // 每个用户都查询一次角色！
        auto roles = co_await dbService_.execSqlCoro(
            "SELECT * FROM sys_role WHERE userId = ?",
            {std::to_string(userId)}
        );

        Json::Value item = userToJson(user);
        item["roles"] = rolesToJson(roles);
        result.append(item);
    }

    co_return result;
}

// ✅ JOIN 查询优化
Task<Json::Value> getUserListGood() {
    auto result = co_await dbService_.execSqlCoro(R"(
        SELECT u.*, GROUP_CONCAT(r.name) as roleNames
        FROM sys_user u
        LEFT JOIN sys_user_role ur ON u.id = ur.userId
        LEFT JOIN sys_role r ON ur.roleId = r.id
        GROUP BY u.id
    )");

    co_return resultToJson(result);
}
```

### 2. 合理使用缓存

```cpp
// ✅ 热点数据缓存
Task<Json::Value> getMenuTree() {
    // 菜单树变化不频繁，缓存 1 小时
    auto cached = co_await cacheManager_.getAllMenus();
    if (cached) {
        co_return *cached;
    }

    auto menus = co_await dbService_.execSqlCoro("SELECT * FROM sys_menu ...");
    auto tree = TreeBuilder::build(menus);

    co_await cacheManager_.cacheAllMenus(tree);
    co_return tree;
}

// ❌ 不要缓存频繁变化的数据
Task<Json::Value> getRealTimeStats() {
    // 实时统计数据不应缓存
    co_return co_await dbService_.execSqlCoro("SELECT COUNT(*) ...");
}
```

### 3. 批量预加载

```cpp
// ✅ 批量预加载缓存
Task<void> warmupCache() {
    // 应用启动时预加载热点数据
    auto menus = co_await getAllMenus();
    co_await cacheManager_.cacheAllMenus(menus);

    auto roles = co_await getAllRoles();
    co_await cacheManager_.cacheAllRoles(roles);

    LOG_INFO << "Cache warmup completed";
}
```

## 监控与调试

### 1. 日志记录

```cpp
// 在 CacheManager 中添加命中率统计
Task<std::optional<Json::Value>> getUserSession(int userId) {
    std::string key = "session:user:" + std::to_string(userId);
    auto result = co_await redis_.getJson(key);

    if (result) {
        LOG_DEBUG << "Cache HIT: " << key;
        metrics_.recordHit();
    } else {
        LOG_DEBUG << "Cache MISS: " << key;
        metrics_.recordMiss();
    }

    co_return result;
}
```

### 2. 性能指标

```cpp
class CacheMetrics {
private:
    std::atomic<int64_t> hits_{0};
    std::atomic<int64_t> misses_{0};

public:
    void recordHit() { hits_++; }
    void recordMiss() { misses_++; }

    double getHitRate() const {
        int64_t total = hits_ + misses_;
        return total > 0 ? static_cast<double>(hits_) / total : 0.0;
    }

    void log() {
        LOG_INFO << "Cache statistics: "
                 << "hits=" << hits_
                 << ", misses=" << misses_
                 << ", hit_rate=" << (getHitRate() * 100) << "%";
    }
};
```

### 3. Redis 监控命令

```bash
# 查看所有键
redis-cli KEYS "*"

# 查看内存使用
redis-cli INFO memory

# 查看缓存命中率
redis-cli INFO stats | grep keyspace

# 实时监控
redis-cli MONITOR

# 查看慢查询
redis-cli SLOWLOG GET 10
```

## 常见问题

### Q1: 事务失败时缓存怎么办？

**A**: 使用 `onCommit()` 回调，只有事务提交成功后才清除缓存。

### Q2: Redis 宕机怎么办？

**A**: RedisService 自动降级，返回 `std::nullopt`，业务层直接查数据库。

### Q3: 如何避免缓存雪崩？

**A**: 给 TTL 添加随机偏移：
```cpp
int ttl = baseTtl + (rand() % 300);  // ±5分钟随机偏移
```

### Q4: 如何处理缓存穿透？

**A**: 缓存空值或使用布隆过滤器：
```cpp
if (!user) {
    // 缓存空值，避免反复查询不存在的数据
    co_await cacheManager_.cacheNull(userId, 60);
}
```

### Q5: 事务超时怎么办？

**A**: Drogon 事务有默认超时，超时自动回滚。保持事务尽可能短。

## 检查清单

开发新功能时，确保：

- [ ] 数据库操作使用 `TransactionGuard`
- [ ] 事务提交成功后清除相关缓存（`onCommit`）
- [ ] 读取数据时优先查缓存
- [ ] 缓存键使用统一命名规范
- [ ] 设置合理的 TTL
- [ ] 异常情况下事务能正确回滚
- [ ] 关键操作记录日志
- [ ] 批量操作使用单个事务

## 参考文档

- [Redis 集成指南](REDIS.md)
- [事务管理指南](TRANSACTION_GUIDE.md)
- [事务使用示例](TRANSACTION_EXAMPLES.md)
- [缓存集成示例](CACHE_INTEGRATION_EXAMPLES.md)
