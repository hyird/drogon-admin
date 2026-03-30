import { type FormInstance } from "antd";
import type { NamePath } from "antd/es/form/interface";
import { z } from "zod";

/**
 * 将空字符串规范化为 undefined，方便 zod 处理可选字段
 */
export function normalizeEmptyString(value: unknown): unknown {
  if (typeof value !== "string") {
    return value;
  }

  const trimmed = value.trim();
  return trimmed.length > 0 ? trimmed : undefined;
}

/**
 * 清空表单当前错误
 */
export function clearFormErrors(form: FormInstance) {
  const fields = form.getFieldsError().map(({ name }) => ({
    name,
    errors: [] as string[],
  }));

  if (fields.length > 0) {
    form.setFields(fields);
  }
}

/**
 * 将 Zod 错误映射到 Ant Design Form
 */
export function applyZodErrors(form: FormInstance, error: z.ZodError) {
  clearFormErrors(form);

  form.setFields(
    error.issues.map((issue) => ({
      name: (issue.path.length > 0 ? issue.path : ["_form"]) as NamePath,
      errors: [issue.message],
    }))
  );
}

/**
 * 使用 Zod 校验表单值并自动写回错误
 */
export function validateWithZod<TSchema extends z.ZodTypeAny>(
  form: FormInstance,
  schema: TSchema,
  values: unknown
): z.output<TSchema> | null {
  const result = schema.safeParse(values);
  if (!result.success) {
    applyZodErrors(form, result.error);
    return null;
  }

  clearFormErrors(form);
  return result.data;
}
