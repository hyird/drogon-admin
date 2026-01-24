import type { ReactNode } from "react";

interface PageContainerProps {
  /** 页面标题区域（搜索栏、操作按钮等） - 固定不滚动 */
  header?: ReactNode;
  /** 页面主体内容 - 可滚动 */
  children: ReactNode;
}

/**
 * 页面容器组件
 * - header: 固定在顶部的搜索/操作栏
 * - children: 可滚动的主体内容
 */
export function PageContainer({ header, children }: PageContainerProps) {
  return (
    <div className="h-full flex flex-col overflow-hidden">
      {header && <div className="shrink-0 p-4 bg-white relative z-10">{header}</div>}
      <div className="flex-1 overflow-auto">{children}</div>
    </div>
  );
}
