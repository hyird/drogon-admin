import { z } from "zod";
import { VALIDATION_REGEX } from "@/utils";
import { normalizeEmptyString } from "./helpers";

function requiredText(label: string, maxLength: number) {
  return z
    .string()
    .trim()
    .min(1, `请输入${label}`)
    .max(maxLength, `${label}长度不能超过${maxLength}位`);
}

function optionalText(maxLength: number) {
  return z.preprocess(
    normalizeEmptyString,
    z.string().trim().max(maxLength, `长度不能超过${maxLength}位`).optional()
  );
}

function optionalPositiveInt() {
  return z.preprocess(normalizeEmptyString, z.coerce.number().int().positive().optional());
}

function optionalNullablePositiveInt() {
  return z.preprocess(
    (value) => {
      if (value === null) {
        return null;
      }
      return normalizeEmptyString(value);
    },
    z.union([z.coerce.number().int().positive(), z.null()]).optional()
  );
}

function optionalNonNegativeInt() {
  return z.preprocess(normalizeEmptyString, z.coerce.number().int().min(0).optional());
}

function requiredPositiveIntArray(message: string) {
  return z.preprocess(
    (value) => (Array.isArray(value) ? value : []),
    z.array(z.coerce.number().int().positive()).min(1, message)
  );
}

export const statusSchema = z.enum(["enabled", "disabled"]);

export const loginSchema = z.object({
  username: requiredText("用户名", 50),
  password: requiredText("密码", 100),
});

export type LoginFormValues = z.infer<typeof loginSchema>;

const userFormBaseSchema = z.object({
  id: optionalPositiveInt(),
  username: requiredText("用户名", 50),
  password: z.preprocess(
    normalizeEmptyString,
    z.string().min(6, "密码长度不能小于6位").max(100, "密码长度不能超过100位").optional()
  ),
  nickname: optionalText(50),
  phone: z.preprocess(
    normalizeEmptyString,
    z.string().regex(VALIDATION_REGEX.PHONE, "请输入正确的手机号").optional()
  ),
  email: z.preprocess(normalizeEmptyString, z.string().email("请输入正确的邮箱").optional()),
  departmentId: optionalNullablePositiveInt(),
  status: statusSchema,
  roleIds: requiredPositiveIntArray("请选择至少一个角色"),
});

export const userFormSchema = userFormBaseSchema.superRefine((data, ctx) => {
  if (!data.id && !data.password) {
    ctx.addIssue({
      code: z.ZodIssueCode.custom,
      path: ["password"],
      message: "请输入密码",
    });
  }
});

export type UserFormValues = z.infer<typeof userFormSchema>;

export const roleFormSchema = z.object({
  id: optionalPositiveInt(),
  name: requiredText("角色名称", 50),
  code: z
    .string()
    .trim()
    .min(1, "请输入角色编码")
    .max(50, "角色编码长度不能超过50位")
    .regex(/^[a-zA-Z][a-zA-Z0-9_]*$/, "编码以字母开头，只能包含字母、数字、下划线"),
  status: statusSchema,
});

export type RoleFormValues = z.infer<typeof roleFormSchema>;

export const departmentFormSchema = z.object({
  id: optionalPositiveInt(),
  name: requiredText("部门名称", 50),
  code: optionalText(50),
  parentId: optionalNullablePositiveInt(),
  order: optionalNonNegativeInt(),
  leaderId: optionalNullablePositiveInt(),
  status: statusSchema,
});

export type DepartmentFormValues = z.infer<typeof departmentFormSchema>;

export const menuTypeSchema = z.enum(["menu", "page", "button"]);

export const menuFormSchema = z
  .object({
    id: optionalPositiveInt(),
    name: requiredText("名称", 50),
    parentId: optionalNullablePositiveInt(),
    type: menuTypeSchema,
    pathSegment: optionalText(120),
    component: optionalText(255),
    icon: optionalText(100),
    order: optionalNonNegativeInt(),
    status: statusSchema,
    permissionCode: optionalText(100),
  })
  .superRefine((data, ctx) => {
    if (data.type === "page" && !data.pathSegment) {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        path: ["pathSegment"],
        message: "请输入路径片段",
      });
    }

    if (data.type === "page" && !data.component) {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        path: ["component"],
        message: "页面必须配置组件标识",
      });
    }

    if (data.type === "button" && !data.permissionCode) {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        path: ["permissionCode"],
        message: "按钮必须配置权限标识",
      });
    }
  });

export type MenuFormValues = z.infer<typeof menuFormSchema>;
