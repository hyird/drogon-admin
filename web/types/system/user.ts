/**
 * 用户管理类型定义
 */

import type { PageParams } from "../common";

// ============ 枚举/状态类型 ============

export type UserStatus = "enabled" | "disabled";

// ============ 基础类型 ============

export interface UserRole {
  id: number;
  name: string;
  code: string;
}

// ============ 列表项/详情类型 ============

export interface UserItem {
  id: number;
  username: string;
  nickname?: string;
  phone?: string;
  email?: string;
  departmentId?: number | null;
  departmentName?: string;
  status: UserStatus;
  roles: UserRole[];
  createdAt?: string;
  updatedAt?: string;
}

// ============ 查询参数 ============

export interface UserQuery extends PageParams {
  status?: UserStatus;
  departmentId?: number;
}

// ============ DTO 类型 ============

export interface CreateUserDto {
  username: string;
  password: string;
  nickname?: string;
  phone?: string;
  email?: string;
  departmentId?: number | null;
  status?: UserStatus;
  roleIds?: number[];
}

export interface UpdateUserDto {
  nickname?: string;
  phone?: string;
  email?: string;
  departmentId?: number | null;
  status?: UserStatus;
  password?: string;
  roleIds?: number[];
}

export interface UpdatePasswordDto {
  oldPassword: string;
  newPassword: string;
}
