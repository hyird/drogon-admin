# 快速上手：Redis 缓存与事务管理

## 5 分钟快速体验

### 1. 安装 Redis

```bash
# Ubuntu/Debian
sudo apt install redis-server
sudo systemctl start redis

# macOS
brew install redis
brew services start redis

# Windows (WSL)
sudo apt install redis-server
redis-server &
```

验证安装：
```bash
redis-cli ping
# 输出: PONG
```

### 2. 配置项目

配置文件已经准备好了，无需修改（使用默认 Redis 端口 6379）。

如需修改，编辑 `config/config.json`：
```json
{
  "redis_clients": [{
    "host": "127.0.0.1",
    "port": 6379,
    "passwd": "",
    "db": 0
  }],
  "custom_config": {
    "cache": {
      "enabled": true
    }
  }
}
```

### 3. 编译运行

```bash
# 编译
cmake --build out/build/release

# 运行
cd build/release
./server  # Linux/macOS
server.exe  # Windows
```

### 4. 测试缓存

#### 测试 1：登录（查看缓存命中）

```bash
# 第一次登录
curl -X POST http://localhost:3000/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username": "admin", "password": "admin123"}'
```

查看日志，应该看到：
```
[DEBUG] User session cache miss for userId: 1
[DEBUG] User roles cache miss for userId: 1
[DEBUG] User menus cache miss for userId: 1
```

使用返回的 token，再次获取用户信息：
```bash
curl http://localhost:3000/api/auth/me \
  -H "Authorization: Bearer YOUR_TOKEN"
```

查看日志，应该看到缓存命中：
```
[DEBUG] User session cache hit for userId: 1
```

#### 测试 2：登录限流

连续输入错误密码 5 次：
```bash
for i in {1..5}; do
  curl -X POST http://localhost:3000/api/auth/login \
    -H "Content-Type: application/json" \
    -d '{"username": "admin", "password": "wrong"}'
done
```

第 6 次会返回：
```json
{
  "code": 429,
  "message": "登录失败次数过多，请15分钟后再试"
}
```

查看 Redis 中的限流键：
```bash
redis-cli GET "login:failed:admin"
# 输出: 5
```

#### 测试 3：缓存失效

更新菜单：
```bash
curl -X PUT http://localhost:3000/api/menus/1 \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"name": "新菜单名称"}'
```

查看日志：
```
[INFO] Cleared menu cache for X users
```

再次获取用户信息，缓存未命中（因为菜单缓存被清除了）：
```
[DEBUG] User menus cache miss for userId: 1
```

### 5. 监控 Redis

#### 实时监控所有命令

```bash
redis-cli MONITOR
```

然后登录系统，观察 Redis 命令：
```
"GET" "session:user:1"
"SETEX" "session:user:1" "3600" "{...}"
"GET" "user:roles:1"
"SETEX" "user:roles:1" "3600" "[...]"
```

#### 查看缓存内容

```bash
# 查看所有键
redis-cli KEYS "*"

# 查看用户会话
redis-cli GET "session:user:1"

# 查看 TTL
redis-cli TTL "session:user:1"
```

## 10 分钟实战：编写带缓存的 API

### 场景：获取部门列表（带缓存）

#### 1. 创建 Department.Service.hpp

```cpp
#pragma once

#include "common/database/DatabaseService.hpp"
#include "common/cache/CacheManager.hpp"
#include "common/utils/TreeBuilder.hpp"

class DepartmentService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    Task<Json::Value> tree() {
        // 1. 先查缓存
        auto cached = co_await cacheManager_.getDepartmentTree();
        if (cached) {
            LOG_DEBUG << "Department tree cache hit";
            co_return *cached;
        }

        LOG_DEBUG << "Department tree cache miss";

        // 2. 查询数据库
        auto result = co_await dbService_.execSqlCoro(
            "SELECT * FROM sys_department WHERE deletedAt IS NULL ORDER BY `order`"
        );

        // 3. 构建树形结构
        Json::Value items(Json::arrayValue);
        for (const auto& row : result) {
            Json::Value item;
            item["id"] = F_INT(row["id"]);
            item["name"] = F_STR(row["name"]);
            item["parentId"] = F_INT(row["parentId"]);
            items.append(item);
        }

        auto tree = TreeBuilder::build(items);

        // 4. 写入缓存（TTL: 1小时）
        co_await cacheManager_.cacheDepartmentTree(tree);

        co_return tree;
    }

    Task<void> update(int id, const Json::Value& data) {
        auto tx = co_await TransactionGuard::create(dbService_);

        co_await tx.execSqlCoro(
            "UPDATE sys_department SET name = ? WHERE id = ?",
            {data["name"].asString(), std::to_string(id)}
        );

        // 提交成功后清除部门树缓存
        tx.onCommit([this]() -> Task<void> {
            co_await cacheManager_.clearDepartmentTree();
        });

        co_await tx.commit();
    }
};
```

#### 2. 在 CacheManager 中添加方法

编辑 `server/common/cache/CacheManager.hpp`：

```cpp
// 在 CacheManager 类中添加：

Task<bool> cacheDepartmentTree(const Json::Value& tree) {
    co_return co_await redis_.setJson("department:tree", tree, 3600);
}

Task<std::optional<Json::Value>> getDepartmentTree() {
    co_return co_await redis_.getJson("department:tree");
}

Task<bool> clearDepartmentTree() {
    co_return co_await redis_.del("department:tree");
}
```

#### 3. 创建 Controller

```cpp
#pragma once

#include <drogon/HttpController.h>
#include "Department.Service.hpp"
#include "common/utils/Response.hpp"

class DepartmentController : public HttpController<DepartmentController> {
private:
    DepartmentService service_;

public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DepartmentController::tree, "/api/departments/tree", Get, "AuthFilter");
    ADD_METHOD_TO(DepartmentController::update, "/api/departments/{id}", Put, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> tree(HttpRequestPtr req) {
        try {
            auto tree = co_await service_.tree();
            co_return Response::ok(tree);
        } catch (const std::exception& e) {
            LOG_ERROR << "Get department tree error: " << e.what();
            co_return Response::internalError("获取部门树失败");
        }
    }

    Task<HttpResponsePtr> update(HttpRequestPtr req, int id) {
        try {
            auto json = req->getJsonObject();
            co_await service_.update(id, *json);
            co_return Response::updated("更新成功");
        } catch (const std::exception& e) {
            LOG_ERROR << "Update department error: " << e.what();
            co_return Response::internalError("更新部门失败");
        }
    }
};
```

#### 4. 测试

```bash
# 第一次请求（缓存未命中）
curl http://localhost:3000/api/departments/tree \
  -H "Authorization: Bearer YOUR_TOKEN"

# 第二次请求（缓存命中）
curl http://localhost:3000/api/departments/tree \
  -H "Authorization: Bearer YOUR_TOKEN"

# 更新部门（清除缓存）
curl -X PUT http://localhost:3000/api/departments/1 \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"name": "新部门名称"}'

# 再次请求（缓存未命中）
curl http://localhost:3000/api/departments/tree \
  -H "Authorization: Bearer YOUR_TOKEN"
```

查看日志：
```
[DEBUG] Department tree cache miss     # 第一次
[DEBUG] Department tree cache hit      # 第二次
[INFO] Cleared department tree cache   # 更新后
[DEBUG] Department tree cache miss     # 更新后再次请求
```

## 常见场景代码片段

### 1. 简单缓存查询

```cpp
Task<Json::Value> getConfig() {
    // 缓存配置数据（TTL: 1小时）
    auto cached = co_await cacheManager_.getConfig();
    if (cached) {
        co_return *cached;
    }

    auto config = co_await dbService_.execSqlCoro("SELECT * FROM sys_config");
    co_await cacheManager_.cacheConfig(config);

    co_return config;
}
```

### 2. 事务 + 缓存

```cpp
Task<void> updateUser(int id, const Json::Value& data) {
    auto tx = co_await TransactionGuard::create(dbService_);

    co_await tx.execSqlCoro("UPDATE sys_user SET name = ? WHERE id = ?", {
        data["name"].asString(),
        std::to_string(id)
    });

    tx.onCommit([this, id]() -> Task<void> {
        co_await cacheManager_.clearUserCache(id);
    });

    co_await tx.commit();
}
```

### 3. 批量操作

```cpp
Task<void> batchDelete(const std::vector<int>& ids) {
    TRANSACTION_DO(dbService_, [this, &ids](auto& tx) -> Task<void> {
        for (int id : ids) {
            co_await tx.execSqlCoro("DELETE FROM sys_user WHERE id = ?", {
                std::to_string(id)
            });
        }

        tx.onCommit([this, &ids]() -> Task<void> {
            for (int id : ids) {
                co_await cacheManager_.clearUserCache(id);
            }
        });
    });
}
```

### 4. 限流

```cpp
Task<HttpResponsePtr> someApi(HttpRequestPtr req) {
    int userId = req->attributes()->get<int>("userId");

    // 每分钟最多 100 次
    auto count = co_await cacheManager_.checkRateLimit(userId, "api:users", 100, 60);
    if (count > 100) {
        co_return Response::error(429, "请求过于频繁", 429);
    }

    // 正常处理
}
```

## 故障排查

### 问题 1：Redis 连接失败

**症状**：
```
[WARN] Failed to get Redis client: connection refused
```

**解决**：
```bash
# 检查 Redis 是否运行
redis-cli ping

# 启动 Redis
redis-server

# 检查端口
netstat -an | grep 6379
```

### 问题 2：缓存未生效

**症状**：日志中看不到 "Cache hit"

**排查**：
```bash
# 检查配置
cat config/config.json | grep cache

# 检查 Redis 中是否有数据
redis-cli KEYS "*"

# 查看日志
tail -f logs/server.log | grep Cache
```

### 问题 3：事务未提交

**症状**：
```
[WARN] Transaction auto-rollback in destructor
```

**原因**：忘记调用 `co_await tx.commit()`

**解决**：确保所有事务都显式提交：
```cpp
auto tx = co_await TransactionGuard::create(dbService_);
// ... 操作 ...
co_await tx.commit();  // 必须调用！
```

## 下一步

- 阅读 [Redis 集成指南](REDIS.md) 了解更多缓存策略
- 阅读 [事务管理指南](TRANSACTION_GUIDE.md) 了解高级用法
- 阅读 [数据库最佳实践](DATABASE_BEST_PRACTICES.md) 了解完整架构
- 查看 [实际示例](TRANSACTION_EXAMPLES.md) 学习更多场景

## 帮助

遇到问题？

1. 查看日志：`tail -f logs/server.log`
2. 查看 Redis：`redis-cli MONITOR`
3. 查看文档：[docs/](.)
4. 提交 Issue
