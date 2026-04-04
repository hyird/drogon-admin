# Drogon Admin

基于 C++ Drogon 框架的后台管理系统，前端使用 React + TypeScript + Ant Design。

## 技术栈

### 后端
- **Drogon** - 高性能 C++ Web 框架
- **C++20** - 使用协程支持异步操作
- **MariaDB/MySQL** - 数据库
- **Redis** - 缓存与会话管理
- **JWT** - 身份认证
- **vcpkg** - 包管理

### 前端
- **React 19** - UI 框架
- **TypeScript** - 类型安全
- **Ant Design 6** - UI 组件库
- **TanStack Query** - 服务端状态管理
- **Redux Toolkit** - 客户端状态管理
- **Framer Motion** - 页面过渡动画
- **React Router 7** - 路由管理
- **Tailwind CSS 4** - 样式
- **Vite 7** - 构建工具

## 功能特性

### 业务功能
- **用户管理** - 用户增删改查、角色分配
- **角色管理** - 角色权限配置
- **菜单管理** - 动态菜单、按钮权限
- **部门管理** - 组织架构树形结构

### 技术特性
- **动态路由** - 基于用户权限动态生成路由
- **页面过渡动画** - 登录/后台全页面切换动画，后台内页面 outlet 区域动画
- **权限控制** - 路由级、按钮级权限控制
- **响应式布局** - 支持桌面端和移动端
- **多标签页** - 支持多页面切换，保持页面状态
- **Redis 缓存** - 会话、权限、菜单数据缓存，性能提升 95%+
- **事务管理** - RAII 风格事务封装，自动回滚，缓存一致性保证
- **登录限流** - 基于 Redis 的登录失败次数限制
- **Token 黑名单** - 支持强制用户下线

## 目录结构

```
├── server/                 # 后端代码
│   ├── main.cpp           # 入口
│   ├── common/            # 公共模块
│   │   ├── cache/         # 缓存管理 (Redis)
│   │   ├── database/      # 数据库服务、事务封装
│   │   ├── filters/       # 过滤器 (认证、权限)
│   │   └── utils/         # 工具类
│   └── modules/           # 业务模块
│       └── system/        # 系统管理
├── web/                   # 前端代码
│   ├── components/        # 公共组件
│   ├── config/            # 配置文件
│   ├── hooks/             # 自定义 Hooks
│   ├── layouts/           # 布局组件
│   ├── pages/             # 页面组件
│   ├── providers/         # Context 提供者
│   ├── routes/            # 路由配置
│   ├── services/          # API 服务层
│   ├── store/             # Redux 状态管理
│   ├── types/             # TypeScript 类型定义
│   └── utils/             # 工具函数
├── config/                # 配置文件
└── CMakeLists.txt         # CMake 构建配置
```

## 快速开始

### 环境要求

- CMake 3.20+
- vcpkg
- Bun (前端构建)
- MariaDB 11.x / MySQL 8.x
- Redis 7.x+（可选，用于缓存）

### 数据库初始化

```bash
mysql -u root -p -e "CREATE DATABASE IF NOT EXISTS admin CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
```

然后启动服务，程序会自动执行迁移并写入默认账号、菜单和部门。

### Redis 安装（可选）

```bash
# Ubuntu/Debian
sudo apt install redis-server

# macOS
brew install redis

# Windows
# 下载 Redis for Windows 或使用 WSL

# 启动 Redis
redis-server
```

### 配置

先复制 `config/config.example.json` 为 `config/config.json`，再修改实际环境配置：

```json
{
  "db_clients": [{
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "passwd": "your_password",
    "dbname": "admin"
  }],
  "redis_clients": [{
    "host": "127.0.0.1",
    "port": 6379,
    "passwd": "",
    "db": 0,
    "is_fast": true
  }],
  "custom_config": {
    "cache": {
      "enabled": true,
      "user_session_ttl": 3600,
      "user_menus_ttl": 1800,
      "user_roles_ttl": 3600
    }
  }
}
```

**注意**：如果不使用 Redis，将 `cache.enabled` 设置为 `false`，系统会自动降级到无缓存模式。

### 构建

```bash
# 配置 (首次)
cmake --preset release

# 编译后端 + 前端
cmake --build out/build/release

# 仅编译后端
cmake --build out/build/release -- -DBUILD_FRONTEND=OFF
```

### 运行

编译后的文件在 `build/release/` 目录:

```bash
cd build/release
./server        # Linux/macOS
server.exe      # Windows
```

访问 http://localhost:3000

### 前端开发

```bash
bun install
bun run dev
```

访问 http://localhost:5173 (代理到后端 3000 端口)

## 性能优化

### 缓存加速

系统使用 Redis 缓存以下数据：

| 缓存项 | 原耗时 | 缓存后 | 优化幅度 |
|--------|--------|--------|----------|
| 用户登录（角色+菜单） | ~80ms | ~2ms | **97.5%** |
| 权限验证 | ~10ms | ~0.5ms | **95%** |
| 菜单加载 | ~50ms | ~1ms | **98%** |
| 用户信息 | ~30ms | ~1ms | **96.7%** |

实现见：[`server/common/cache/CacheManager.hpp`](server/common/cache/CacheManager.hpp) 和 [`server/common/database/RedisService.hpp`](server/common/database/RedisService.hpp)

### 事务管理

使用 RAII 风格的 `TransactionGuard` 确保：

- **异常安全**：异常时自动回滚
- **缓存一致性**：事务提交成功后才清除缓存
- **明确语义**：必须显式调用 `commit()`

```cpp
auto tx = co_await TransactionGuard::create(dbService_);
co_await tx.execSqlCoro("UPDATE sys_user ...", {...});

tx.onCommit([this, userId]() -> Task<void> {
    co_await cacheManager_.clearUserCache(userId);
});

co_await tx.commit();
```

实现见：[`server/common/database/TransactionGuard.hpp`](server/common/database/TransactionGuard.hpp)

## 相关文件

- [开发规范](AGENTS.md) - 项目编码规范
- [`config/config.example.json`](config/config.example.json) - 配置模板
- [`server/common/database/migrations/README.md`](server/common/database/migrations/README.md) - 数据库迁移约定
- [`server/common/database/TransactionGuard.hpp`](server/common/database/TransactionGuard.hpp) - 事务封装实现
- [`server/common/cache/CacheManager.hpp`](server/common/cache/CacheManager.hpp) - 缓存管理实现

历史上的 `docs/` 目录已清理，相关说明现在集中在 README、`AGENTS.md` 和源码注释中。

## License

MIT
