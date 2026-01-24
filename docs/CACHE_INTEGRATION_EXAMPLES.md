# 缓存集成示例

## User.Service.hpp 中清除用户缓存

在用户信息或角色变更时清除缓存：

```cpp
#include "common/cache/CacheManager.hpp"

class UserService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;  // 添加缓存管理器

public:
    // 更新用户信息时清除缓存
    Task<void> update(int id, const Json::Value& data) {
        co_await detail(id);
        co_await checkNotBuiltinAdmin(id);

        // ... 更新数据库 ...

        // 清除用户缓存
        co_await cacheManager_.clearUserCache(id);
    }

    // 分配角色时清除缓存
    Task<void> assignRoles(int userId, const Json::Value& roleIds) {
        // ... 更新角色关联 ...

        // 清除用户角色和菜单缓存
        co_await cacheManager_.deleteUserRoles(userId);
        co_await cacheManager_.deleteUserMenus(userId);
        co_await cacheManager_.deleteUserSession(userId);
    }

    // 删除用户时清除缓存
    Task<void> remove(int id) {
        // ... 软删除用户 ...

        // 清除用户所有缓存
        co_await cacheManager_.clearUserCache(id);
    }
};
```

## Role.Service.hpp 中清除角色缓存

角色权限变更影响所有拥有该角色的用户：

```cpp
#include "common/cache/CacheManager.hpp"

class RoleService {
private:
    DatabaseService dbService_;
    CacheManager cacheManager_;

public:
    // 更新角色权限时清除所有相关用户缓存
    Task<void> updatePermissions(int roleId, const Json::Value& menuIds) {
        // ... 更新角色菜单关联 ...

        // 清除所有用户的角色和菜单缓存
        co_await cacheManager_.clearAllUserRolesCache();
        co_await cacheManager_.clearAllUserMenusCache();
    }

    // 更新角色信息
    Task<void> update(int id, const Json::Value& data) {
        // ... 更新数据库 ...

        // 如果状态变更（启用/禁用），清除所有用户缓存
        if (data.isMember("status")) {
            co_await cacheManager_.clearAllUserRolesCache();
            co_await cacheManager_.clearAllUserMenusCache();
        }
    }
};
```

## AuthFilter.hpp 中检查 Token 黑名单

在认证过滤器中检查 Token 是否被强制失效：

```cpp
#include "common/cache/CacheManager.hpp"

class AuthFilter : public HttpFilter<AuthFilter> {
private:
    CacheManager cacheManager_;

public:
    Task<void> doFilter(const HttpRequestPtr& req,
                       FilterCallback&& cb,
                       FilterChainCallback&& ccb) override {
        std::string token = extractToken(req);

        // 检查 Token 是否在黑名单中
        bool isBlacklisted = co_await cacheManager_.isTokenBlacklisted(token);
        if (isBlacklisted) {
            auto resp = Response::unauthorized("Token 已失效，请重新登录");
            cb(resp);
            co_return;
        }

        // ... 验证 Token ...

        ccb();
    }
};
```

## 添加登出接口

在 AuthController 中添加登出功能，将 Token 加入黑名单：

```cpp
Task<HttpResponsePtr> logout(HttpRequestPtr req) {
    try {
        auto attrs = req->attributes();
        int userId = attrs->get<int>("userId");
        std::string token = extractToken(req);

        // 计算 Token 剩余有效期
        auto payload = jwtUtils_->verify(token);
        int exp = payload["exp"].asInt();
        int now = std::time(nullptr);
        int remainingTtl = exp - now;

        if (remainingTtl > 0) {
            // 将 Token 加入黑名单
            co_await cacheManager_.blacklistToken(token, remainingTtl);
        }

        // 清除用户会话缓存
        co_await cacheManager_.deleteUserSession(userId);

        co_return Response::ok("登出成功");

    } catch (const std::exception& e) {
        LOG_ERROR << "Logout error: " << e.what();
        co_return Response::internalError("登出失败");
    }
}
```

## 缓存预热（可选）

在应用启动时预加载热点数据：

```cpp
// main.cpp
#include "common/cache/CacheManager.hpp"

Task<void> warmupCache() {
    CacheManager cacheManager;

    // 预加载全局菜单
    DatabaseService dbService;
    auto menusSql = "SELECT * FROM sys_menu WHERE status = 'enabled' AND deletedAt IS NULL";
    auto menusResult = co_await dbService.execSqlCoro(menusSql);

    Json::Value menus(Json::arrayValue);
    for (const auto& row : menusResult) {
        // ... 构建菜单数据 ...
    }

    co_await cacheManager.cacheAllMenus(menus);
    LOG_INFO << "Cache warmup completed";
}

int main() {
    app().registerBeginningAdvice([]() {
        drogon::async_run([]() -> Task<void> {
            co_await warmupCache();
        });
    });

    app().run();
}
```

## 性能监控示例

记录缓存命中率到日志：

```cpp
class CacheMetrics {
private:
    std::atomic<int64_t> hits_{0};
    std::atomic<int64_t> misses_{0};

public:
    void recordHit() { hits_++; }
    void recordMiss() { misses_++; }

    void logStats() {
        int64_t total = hits_ + misses_;
        if (total > 0) {
            double hitRate = static_cast<double>(hits_) / total * 100;
            LOG_INFO << "Cache hit rate: " << hitRate << "% ("
                     << hits_ << "/" << total << ")";
        }
    }
};
```

## 完整工作流示例：用户登录

```cpp
Task<HttpResponsePtr> login(HttpRequestPtr req) {
    auto json = req->getJsonObject();
    std::string username = (*json)["username"].asString();
    std::string password = (*json)["password"].asString();

    // 1. 检查登录失败次数（Redis）
    auto failureCount = co_await cacheManager_.getLoginFailureCount(username);
    if (failureCount >= 5) {
        co_return Response::error(429, "登录失败次数过多，请15分钟后再试", 429);
    }

    // 2. 验证用户名密码（Database）
    auto user = co_await queryUser(username);
    if (!user || !verifyPassword(password, user.passwordHash)) {
        // 记录失败次数（Redis）
        co_await cacheManager_.recordLoginFailure(username);
        throw AuthException::PasswordIncorrect();
    }

    // 3. 登录成功，清除失败记录（Redis）
    co_await cacheManager_.clearLoginFailure(username);

    // 4. 生成 Token
    std::string token = jwtUtils_->sign({{"userId", user.id}});

    // 5. 构建用户信息（包含角色、菜单）
    auto userInfo = co_await buildUserInfo(user.id);

    // 6. 缓存用户会话（Redis）
    co_await cacheManager_.cacheUserSession(user.id, userInfo);

    // 7. 返回登录结果
    Json::Value data;
    data["token"] = token;
    data["user"] = userInfo;

    co_return Response::ok(data, "登录成功");
}
```

## 完整工作流示例：获取用户信息

```cpp
Task<HttpResponsePtr> getCurrentUser(HttpRequestPtr req) {
    int userId = req->attributes()->get<int>("userId");

    // 1. 先从缓存获取（Redis）
    auto cached = co_await cacheManager_.getUserSession(userId);
    if (cached) {
        LOG_DEBUG << "Cache hit for userId: " << userId;
        co_return Response::ok(*cached);  // 直接返回，无需查数据库
    }

    LOG_DEBUG << "Cache miss for userId: " << userId;

    // 2. 缓存未命中，查询数据库
    auto user = co_await queryUser(userId);

    // 3. 构建完整用户信息（查询角色、菜单等）
    auto userInfo = co_await buildUserInfo(userId);

    // 4. 写入缓存（Redis）
    co_await cacheManager_.cacheUserSession(userId, userInfo);

    // 5. 返回结果
    co_return Response::ok(userInfo);
}
```

## 缓存失效场景总结

| 操作 | 需要清除的缓存 | 方法调用 |
|------|---------------|---------|
| 更新用户基本信息 | 用户会话 | `clearUserCache(userId)` |
| 分配/移除用户角色 | 用户会话、角色、菜单 | `clearUserCache(userId)` |
| 更新角色权限 | 所有用户角色、菜单 | `clearAllUserRolesCache()` + `clearAllUserMenusCache()` |
| 更新菜单 | 所有用户菜单 | `clearAllUserMenusCache()` |
| 用户登出 | 用户会话 + Token黑名单 | `deleteUserSession(userId)` + `blacklistToken(token)` |
| 禁用用户 | 用户所有缓存 + Token黑名单 | `clearUserCache(userId)` + `blacklistToken(token)` |
