import { useState, useMemo, useEffect } from "react";
import {
  Button,
  Form,
  Input,
  Modal,
  Select,
  Space,
  Table,
  Tag,
  TreeSelect,
  InputNumber,
  App,
  Result,
  Switch,
  type TreeSelectProps,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useDebounceFn } from "ahooks";
import type { Menu } from "@/types";
import { menuApi } from "@/services";
import { usePermission } from "@/hooks";
import { useAuthStore, usePermissionStore } from "@/store/hooks";
import { MenuTypeMap } from "@/types/constants";
import { store } from "@/store";
import { filterMenuTree, flattenTree, getPathSegment, normalizeNullablePositiveId } from "@/utils";
import { StatusTag } from "@/components/StatusTag";
import { getRegisteredPages, getRegisteredPermissions, getPageConfig } from "@/config/registry";
import { PageContainer } from "@/components/PageContainer";
import { menuFormSchema, validateWithZod, type MenuFormValues } from "@/validation";
import { ICON_OPTIONS, renderIcon } from "@/utils/icon";

const { Search } = Input;

function typeTag(type: Menu.Type) {
  const config = MenuTypeMap[type];
  return <Tag color={config.color}>{config.text}</Tag>;
}

// 过滤掉按钮类型的树节点
function filterOutButtons(nodes: Menu.TreeItem[]): Menu.TreeItem[] {
  return nodes
    .filter((node) => node.type !== "button")
    .map((node) => {
      const filteredChildren = node.children ? filterOutButtons(node.children) : undefined;
      return {
        ...node,
        children: filteredChildren && filteredChildren.length > 0 ? filteredChildren : undefined,
      };
    });
}

const SystemMenuPage = () => {
  const [keyword, setKeyword] = useState("");
  const [hideButtons, setHideButtons] = useState(false);
  const [modalVisible, setModalVisible] = useState(false);
  const [editing, setEditing] = useState<Menu.TreeItem | null>(null);
  const [form] = Form.useForm<MenuFormValues>();
  const queryClient = useQueryClient();
  const { message, modal } = App.useApp();

  // 权限检查
  const canQuery = usePermission("system:menu:query");
  const canAdd = usePermission("system:menu:add");
  const canEdit = usePermission("system:menu:edit");
  const canDelete = usePermission("system:menu:delete");

  const { run: handleSearch } = useDebounceFn((value: string) => setKeyword(value), { wait: 300 });

  // 菜单树 - 需要查询权限
  const { data: rawMenuTree = [], isLoading } = useQuery({
    queryKey: ["menus", "tree", { all: true }],
    queryFn: () => menuApi.getTree(),
    enabled: canQuery,
  });

  const menuMap = useMemo(() => {
    const flattened = flattenTree(rawMenuTree);
    return Object.fromEntries(flattened.map((item) => [item.id, item]));
  }, [rawMenuTree]);
  const menuTree = useMemo(() => {
    let tree = filterMenuTree(rawMenuTree, keyword);
    if (hideButtons) {
      tree = filterOutButtons(tree);
    }
    return tree;
  }, [rawMenuTree, keyword, hideButtons]);

  const parentTreeData = useMemo((): TreeSelectProps["treeData"] => {
    const loop = (nodes: Menu.TreeItem[]): TreeSelectProps["treeData"] =>
      nodes
        .filter((n) => n.type !== "button")
        .map((n) => ({
          title: `${n.name} (${n.type})`,
          value: n.id,
          children: n.children ? loop(n.children) : undefined,
        }));
    return loop(rawMenuTree);
  }, [rawMenuTree]);

  const watchParentId = Form.useWatch("parentId", form) as number | null | undefined;
  const watchType = Form.useWatch("type", form) as Menu.Type | undefined;
  const watchComponent = Form.useWatch("component", form) as string | undefined;
  const watchIcon = Form.useWatch("icon", form) as string | undefined;

  const parentType: Menu.Type | undefined = useMemo(() => {
    if (!watchParentId) return undefined;
    return menuMap[watchParentId]?.type;
  }, [watchParentId, menuMap]);

  const availableTypes: Menu.Type[] = useMemo(() => {
    if (!watchParentId) {
      return ["menu", "page"];
    }
    if (parentType === "menu") {
      return ["menu", "page"];
    }
    if (parentType === "page") {
      return ["button"];
    }
    return ["menu", "page"];
  }, [watchParentId, parentType]);

  // 获取父级页面的可用权限列表
  const availablePermissions = useMemo(() => {
    // 如果父级不是 page 类型,或者当前类型不是 button,返回所有权限
    if (parentType !== "page" || watchType !== "button") {
      return getRegisteredPermissions();
    }

    // 获取父级菜单项
    const parentMenu = watchParentId ? menuMap[watchParentId] : undefined;
    if (!parentMenu?.component) {
      return getRegisteredPermissions();
    }

    // 从注册中心获取该页面配置
    const pageConfig = getPageConfig(parentMenu.component);
    if (!pageConfig?.permissions) {
      return [];
    }

    // 返回该页面下的权限列表 (需要补全 module 和 resource 字段用于显示)
    return pageConfig.permissions.map((perm) => ({
      ...perm,
      module: pageConfig.module,
      resource: pageConfig.name,
    }));
  }, [parentType, watchType, watchParentId, menuMap]);

  const selectedPageConfig = useMemo(() => {
    if (watchType !== "page" || !watchComponent) {
      return undefined;
    }
    return getPageConfig(watchComponent);
  }, [watchType, watchComponent]);

  // 当可选类型改变时,如果当前类型不在可选项中,自动设置为第一个可选项
  useEffect(() => {
    if (watchType && !availableTypes.includes(watchType)) {
      form.setFieldValue("type", availableTypes[0]);
    }
  }, [availableTypes, watchType, form]);

  // 页面图标由 registerPage 自动提供,不允许手工修改
  useEffect(() => {
    if (watchType === "page") {
      const nextIcon = selectedPageConfig?.icon;
      if (watchIcon !== nextIcon) {
        form.setFieldValue("icon", nextIcon);
      }
      return;
    }

    if (watchType === "button" && watchIcon) {
      form.setFieldValue("icon", undefined);
    }
  }, [watchType, selectedPageConfig?.icon, watchIcon, form]);

  const { refreshUser } = useAuthStore();
  const { setPermissions } = usePermissionStore();

  const syncAuthAfterMenuChange = async () => {
    await refreshUser().unwrap();
    const newUser = store.getState().auth.user;
    if (newUser?.menus && newUser?.roles) {
      setPermissions(newUser.menus, newUser.roles);
    }
  };

  const saveMutation = useMutation({
    mutationFn: async (values: MenuFormValues) => {
      const parent =
        values.parentId !== undefined && values.parentId !== null
          ? menuMap[values.parentId]
          : undefined;
      const pageIcon =
        values.type === "page" ? getPageConfig(values.component || "")?.icon : undefined;
      const finalIcon =
        values.type === "page" ? pageIcon : values.type === "button" ? undefined : values.icon;

      let finalPath: string | null = null;
      if (values.type === "menu" || values.type === "page") {
        const seg = (values.pathSegment || "").trim();
        if (seg) {
          const normalizedSeg = seg.startsWith("/") ? seg : `/${seg}`;
          const parentPath = (parent?.path || "").trim();
          if (parentPath) {
            finalPath = `${parentPath.replace(/\/$/, "")}${normalizedSeg}`;
          } else {
            finalPath = normalizedSeg;
          }
        } else {
          finalPath = null;
        }
      } else {
        finalPath = null;
      }

      if (values.id) {
        const payload: Menu.UpdateDto = {
          name: values.name,
          path: finalPath ?? undefined,
          component: values.component,
          icon: finalIcon,
          parentId: values.parentId === undefined ? null : values.parentId,
          order: values.order,
          type: values.type,
          status: values.status,
          permissionCode: values.permissionCode,
        };
        await menuApi.update(values.id, payload);
        return;
      }

      const payload: Menu.CreateDto = {
        name: values.name,
        path: finalPath ?? undefined,
        component: values.component,
        icon: finalIcon,
        parentId: values.parentId === undefined ? null : values.parentId,
        order: values.order,
        type: values.type,
        status: values.status,
        permissionCode: values.permissionCode,
      };
      await menuApi.create(payload);
    },
    onSuccess: async () => {
      message.success("保存成功");
      setModalVisible(false);
      queryClient.invalidateQueries({ queryKey: ["menus", "tree"] });
      await syncAuthAfterMenuChange();
    },
  });

  const deleteMutation = useMutation({
    mutationFn: (id: number) => menuApi.remove(id),
    onSuccess: async () => {
      message.success("删除成功");
      queryClient.invalidateQueries({ queryKey: ["menus", "tree"] });
      await syncAuthAfterMenuChange();
    },
  });

  const openCreateModal = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({
      type: "menu",
      status: "enabled",
      order: 0,
      parentId: null,
    });
    setModalVisible(true);
  };

  const openEditModal = (record: Menu.TreeItem) => {
    setEditing(record);
    const pathSegment =
      record.type === "menu" || record.type === "page" ? getPathSegment(record, menuMap) : "";
    form.setFieldsValue({
      id: record.id,
      name: record.name,
      pathSegment,
      component: record.component,
      icon: record.icon,
      parentId: normalizeNullablePositiveId(record.parentId),
      order: record.order,
      type: record.type,
      status: record.status,
      permissionCode: record.permissionCode,
    });
    setModalVisible(true);
  };

  const onDelete = (record: Menu.TreeItem) => {
    modal.confirm({
      title: `确认删除「${record.name}」吗？`,
      content: "若存在子菜单，请先删除子菜单。",
      onOk: () => deleteMutation.mutate(record.id),
    });
  };

  const onFinish = (values: MenuFormValues) => {
    const parsed = validateWithZod(form, menuFormSchema, values);
    if (!parsed) return;

    saveMutation.mutate(parsed);
  };

  const handleModalAfterOpen = (open: boolean) => {
    if (!open) {
      form.resetFields();
      setEditing(null);
    }
  };

  // 无查询权限时显示提示
  if (!canQuery) {
    return (
      <PageContainer>
        <Result status="403" title="无权限" subTitle="您没有查询菜单列表的权限，请联系管理员" />
      </PageContainer>
    );
  }

  const columns: ColumnsType<Menu.TreeItem> = [
    {
      title: "名称",
      dataIndex: "name",
      minWidth: 180,
    },
    {
      title: "类型",
      dataIndex: "type",
      render: (t: Menu.Type) => typeTag(t),
    },
    {
      title: "图标",
      dataIndex: "icon",
      render: (icon: string | undefined) =>
        icon ? (
          <Space>
            {renderIcon(icon)}
            <span style={{ color: "#999", fontSize: 12 }}>{icon}</span>
          </Space>
        ) : (
          "-"
        ),
    },
    {
      title: "完整路由",
      dataIndex: "path",
      render: (v: string | null, record) => (record.type === "button" ? "-" : v || "-"),
    },
    {
      title: "组件标识",
      dataIndex: "component",
      render: (v: string | undefined, record) => (record.type === "page" ? v || "-" : "-"),
    },
    {
      title: "权限标识",
      dataIndex: "permissionCode",
      minWidth: 120,
      render: (v: string | undefined, record) =>
        record.type === "button" ? v || <span style={{ color: "#faad14" }}>未配置</span> : "-",
    },
    {
      title: "排序",
      dataIndex: "order",
      width: 80,
    },
    {
      title: "状态",
      dataIndex: "status",
      width: 100,
      render: (v: Menu.Status) => <StatusTag status={v} />,
    },
    {
      title: "操作",
      key: "actions",
      width: 200,
      render: (_, record) => (
        <Space>
          {canEdit && (
            <Button type="link" onClick={() => openEditModal(record)}>
              编辑
            </Button>
          )}
          {canDelete && (
            <Button type="link" danger onClick={() => onDelete(record)}>
              删除
            </Button>
          )}
        </Space>
      ),
    },
  ];

  return (
    <PageContainer
      header={
        <div className="flex items-center justify-between">
          <h3 className="text-base font-medium m-0">菜单管理</h3>
          <Space>
            <Search
              allowClear
              placeholder="按名称 / 路由搜索（树状过滤）"
              onChange={(e) => handleSearch(e.target.value)}
              style={{ width: 280 }}
            />
            <Switch
              checked={!hideButtons}
              onChange={(checked) => setHideButtons(!checked)}
              checkedChildren="显示权限"
              unCheckedChildren="隐藏权限"
            />
            {canAdd && (
              <Button type="primary" onClick={openCreateModal}>
                新建菜单
              </Button>
            )}
          </Space>
        </div>
      }
    >
      <Table<Menu.TreeItem>
        rowKey="id"
        columns={columns}
        dataSource={menuTree}
        loading={isLoading}
        pagination={false}
        size="middle"
        expandable={{
          defaultExpandAllRows: true,
          rowExpandable: (record) => Array.isArray(record.children) && record.children.length > 0,
        }}
        sticky
      />

      <Modal
        open={modalVisible}
        title={editing ? "编辑菜单" : "新建菜单"}
        onCancel={() => {
          setModalVisible(false);
        }}
        onOk={() => form.submit()}
        confirmLoading={saveMutation.isPending}
        afterOpenChange={handleModalAfterOpen}
        destroyOnHidden
        width={640}
      >
        <Form<MenuFormValues> form={form} layout="vertical" onFinish={onFinish}>
          <Form.Item name="id" hidden>
            <Input />
          </Form.Item>

          <Form.Item label="名称" name="name" required>
            <Input placeholder="菜单名称 / 页面标题 / 按钮名称" />
          </Form.Item>

          <Form.Item label="父级菜单" name="parentId">
            <TreeSelect
              allowClear
              treeData={parentTreeData}
              placeholder="不选则为顶级"
              treeDefaultExpandAll
            />
          </Form.Item>

          <Form.Item label="类型" name="type" required>
            <Select>
              {availableTypes.map((t) => (
                <Select.Option key={t} value={t}>
                  {t === "menu" ? "菜单" : t === "page" ? "页面" : "按钮"}
                </Select.Option>
              ))}
            </Select>
          </Form.Item>

          {(watchType === "menu" || watchType === "page") && (
            <Form.Item label="路径片段" name="pathSegment" required={watchType === "page"}>
              <Input placeholder="例如：system / user / menu（不必带父路径）" />
            </Form.Item>
          )}

          {watchType === "page" && (
            <Form.Item label="组件标识" name="component" required>
              <Select
                showSearch
                allowClear
                placeholder="选择页面组件"
                optionFilterProp="label"
                options={getRegisteredPages().map((page) => ({
                  label: `${page.name} (${page.component})`,
                  value: page.component,
                  description: page.description,
                  icon: page.icon,
                }))}
                optionRender={(option) => (
                  <div>
                    <div className="inline-flex items-center gap-1">
                      {renderIcon(option.data.icon)}
                      <span>{option.label}</span>
                    </div>
                    {option.data.description && (
                      <div style={{ fontSize: 12, color: "#999" }}>{option.data.description}</div>
                    )}
                  </div>
                )}
              />
            </Form.Item>
          )}

          {watchType === "button" && (
            <Form.Item label="权限标识" name="permissionCode" required>
              <Select
                showSearch
                allowClear
                placeholder="选择权限标识"
                filterOption={(input, option) =>
                  (option?.label ?? "").toLowerCase().includes(input.toLowerCase()) ||
                  (option?.value ?? "").toLowerCase().includes(input.toLowerCase())
                }
                options={availablePermissions.map((perm) => ({
                  label: `${perm.name} (${perm.code})`,
                  value: perm.code,
                  module: perm.module,
                  resource: perm.resource,
                  description: perm.description,
                }))}
                optionRender={(option) => (
                  <div>
                    <div>
                      <Tag color="blue" style={{ marginRight: 4 }}>
                        {option.data.module}
                      </Tag>
                      <Tag color="green" style={{ marginRight: 4 }}>
                        {option.data.resource}
                      </Tag>
                      {option.data.label}
                    </div>
                    {option.data.description && (
                      <div style={{ fontSize: 12, color: "#999", marginTop: 2 }}>
                        {option.data.description}
                      </div>
                    )}
                  </div>
                )}
              />
            </Form.Item>
          )}

          {watchType === "menu" && (
            <Form.Item label="图标" name="icon">
              <Select
                showSearch
                allowClear
                placeholder="选择菜单图标"
                optionFilterProp="label"
                options={ICON_OPTIONS}
                optionRender={(option) => (
                  <div className="inline-flex items-center gap-1">
                    {renderIcon(option.data.value)}
                    <span>{option.label}</span>
                  </div>
                )}
              />
            </Form.Item>
          )}

          {watchType === "page" && (
            <Form.Item label="图标" name="icon">
              <Input
                disabled
                placeholder={
                  selectedPageConfig?.icon
                    ? `由 registerPage 自动分配: ${selectedPageConfig.icon}`
                    : "由 registerPage 自动分配"
                }
              />
            </Form.Item>
          )}

          <Form.Item label="排序" name="order">
            <InputNumber style={{ width: "100%" }} />
          </Form.Item>
          <Form.Item label="状态" name="status" required>
            <Select>
              <Select.Option value="enabled">启用</Select.Option>
              <Select.Option value="disabled">禁用</Select.Option>
            </Select>
          </Form.Item>
        </Form>
      </Modal>
    </PageContainer>
  );
};

export default SystemMenuPage;
