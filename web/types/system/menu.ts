/**
 * 菜单管理类型定义
 */

// ============ 枚举/状态类型 ============

export type MenuType = "menu" | "page" | "button";
export type MenuStatus = "enabled" | "disabled";

// ============ 列表项/详情类型 ============

export interface MenuItem {
  id: number;
  name: string;
  path?: string | null;
  component?: string;
  icon?: string;
  parentId?: number | null;
  order: number;
  type: MenuType;
  status: MenuStatus;
  permissionCode?: string;
}

export interface MenuTreeItem extends MenuItem {
  children?: MenuTreeItem[];
  fullPath?: string;
}

// ============ 查询参数 ============

export interface MenuQuery {
  keyword?: string;
  status?: MenuStatus;
}

// ============ DTO 类型 ============

export interface CreateMenuDto {
  name: string;
  path?: string;
  component?: string;
  icon?: string;
  parentId?: number | null;
  order?: number;
  type?: MenuType;
  status?: MenuStatus;
  permissionCode?: string;
}

export interface UpdateMenuDto {
  name?: string;
  path?: string;
  component?: string;
  icon?: string;
  parentId?: number | null;
  order?: number;
  type?: MenuType;
  status?: MenuStatus;
  permissionCode?: string;
}
