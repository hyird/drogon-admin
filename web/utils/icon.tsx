import {
  ApartmentOutlined,
  ClockCircleOutlined,
  CloudServerOutlined,
  DesktopOutlined,
  DownOutlined,
  HomeOutlined,
  LogoutOutlined,
  MenuFoldOutlined,
  MenuOutlined,
  MenuUnfoldOutlined,
  PlusOutlined,
  SettingOutlined,
  TeamOutlined,
  UserOutlined,
} from "@ant-design/icons";
import type { AntdIconProps } from "@ant-design/icons/lib/components/AntdIcon";
import type { ComponentType, ReactNode } from "react";

const iconComponents = {
  ApartmentOutlined,
  ClockCircleOutlined,
  CloudServerOutlined,
  DesktopOutlined,
  DownOutlined,
  HomeOutlined,
  LogoutOutlined,
  MenuFoldOutlined,
  MenuOutlined,
  MenuUnfoldOutlined,
  PlusOutlined,
  SettingOutlined,
  TeamOutlined,
  UserOutlined,
} as const satisfies Record<string, ComponentType<AntdIconProps>>;

export type IconName = keyof typeof iconComponents;

export type PageIconName =
  | "HomeOutlined"
  | "UserOutlined"
  | "TeamOutlined"
  | "ApartmentOutlined"
  | "MenuOutlined"
  | "SettingOutlined";

export const ICON_NAMES = Object.keys(iconComponents) as IconName[];

export const ICON_OPTIONS: Array<{ label: IconName; value: IconName }> = ICON_NAMES.map(
  (name) => ({
    label: name,
    value: name,
  })
);

export function renderIcon(iconName?: string, props: AntdIconProps = {}): ReactNode {
  if (!iconName) return null;

  const IconComp = iconComponents[iconName as IconName];

  if (!IconComp) {
    if (import.meta.env.DEV) {
      // eslint-disable-next-line no-console
      console.warn(`[Icon] Icon "${iconName}" not found in the static registry`);
    }
    return null;
  }

  return <IconComp {...props} />;
}
