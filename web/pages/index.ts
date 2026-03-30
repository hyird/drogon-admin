/**
 * 页面注册入口
 * 统一维护页面清单并执行模块注册
 */

import { registerModule, type ModuleConfig } from "@/config/registry";

const pageModules = [
  {
    name: "首页",
    pages: [
      {
        icon: "HomeOutlined",
        component: "Home",
        name: "首页",
        description: "系统概览和数据统计",
        loader: () => import("./Home"),
        permissions: [
          {
            code: "home:dashboard:query",
            name: "查看统计",
            description: "查看首页统计数据和系统信息",
            action: "query",
          },
        ],
      },
    ],
  },
  {
    name: "系统管理",
    pages: [
      {
        icon: "UserOutlined",
        component: "User",
        name: "用户管理",
        description: "系统用户的增删改查、角色分配",
        loader: () => import("./system/User"),
        permissions: [
          { code: "system:user:query", name: "查询用户", action: "query" },
          { code: "system:user:add", name: "新增用户", action: "add" },
          { code: "system:user:edit", name: "编辑用户", action: "edit" },
          { code: "system:user:delete", name: "删除用户", action: "delete" },
        ],
      },
      {
        icon: "TeamOutlined",
        component: "Role",
        name: "角色管理",
        description: "角色的增删改查、权限分配",
        loader: () => import("./system/Role"),
        permissions: [
          { code: "system:role:query", name: "查询角色", action: "query" },
          { code: "system:role:add", name: "新增角色", action: "add" },
          { code: "system:role:edit", name: "编辑角色", action: "edit" },
          { code: "system:role:delete", name: "删除角色", action: "delete" },
          { code: "system:role:perm", name: "分配权限", action: "perm" },
        ],
      },
      {
        icon: "ApartmentOutlined",
        component: "Dept",
        name: "部门管理",
        description: "组织架构的树形管理",
        loader: () => import("./system/Department"),
        permissions: [
          { code: "system:dept:query", name: "查询部门", action: "query" },
          { code: "system:dept:add", name: "新增部门", action: "add" },
          { code: "system:dept:edit", name: "编辑部门", action: "edit" },
          { code: "system:dept:delete", name: "删除部门", action: "delete" },
        ],
      },
      {
        icon: "MenuOutlined",
        component: "Menu",
        name: "菜单管理",
        description: "菜单和权限按钮的配置",
        loader: () => import("./system/Menu"),
        permissions: [
          { code: "system:menu:query", name: "查询菜单", action: "query" },
          { code: "system:menu:add", name: "新增菜单", action: "add" },
          { code: "system:menu:edit", name: "编辑菜单", action: "edit" },
          { code: "system:menu:delete", name: "删除菜单", action: "delete" },
        ],
      },
    ],
  },
] satisfies ModuleConfig[];

pageModules.forEach(registerModule);
