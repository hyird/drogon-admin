import { useState, useMemo } from "react";
import { Button, Form, Input, Modal, Select, Space, Table, Tag, Tree, App, Result } from "antd";
import type { ColumnsType, TablePaginationConfig } from "antd/es/table";
import type { TreeDataNode, TreeProps } from "antd";
import { useDebounceFn } from "ahooks";
import type { Role, Menu } from "@/types";
import { usePermission } from "@/hooks";
import { StatusTag } from "@/components/StatusTag";
import { PageContainer } from "@/components/PageContainer";
import {
  useRoleList,
  useRoleDetail,
  useRoleSave,
  useRoleDelete,
  roleApi,
  useMenuTree,
} from "@/services";
import { roleFormSchema, validateWithZod, type RoleFormValues } from "@/validation";

const { Search } = Input;

function menuTreeToTreeData(menus: Menu.TreeItem[]): TreeDataNode[] {
  return menus.map((m) => ({
    key: m.id,
    title: `${m.name}${m.type === "button" ? " [按钮]" : m.type === "page" ? " [页面]" : ""}`,
    children: m.children?.length ? menuTreeToTreeData(m.children) : undefined,
  }));
}

function getAllTreeKeys(menus: Menu.TreeItem[]): number[] {
  const keys: number[] = [];
  const traverse = (nodes: Menu.TreeItem[]) => {
    nodes.forEach((n) => {
      keys.push(n.id);
      if (n.children?.length) traverse(n.children);
    });
  };
  traverse(menus);
  return keys;
}

function getParentKeys(menus: Menu.TreeItem[]): Set<number> {
  const parentKeys = new Set<number>();
  const traverse = (nodes: Menu.TreeItem[]) => {
    nodes.forEach((n) => {
      if (n.children?.length) {
        parentKeys.add(n.id);
        traverse(n.children);
      }
    });
  };
  traverse(menus);
  return parentKeys;
}

const SystemRolePage = () => {
  const [keyword, setKeyword] = useState("");
  const [pagination, setPagination] = useState({ page: 1, pageSize: 10 });
  const [modalVisible, setModalVisible] = useState(false);
  const [permModalVisible, setPermModalVisible] = useState(false);
  const [editing, setEditing] = useState<Role.Item | null>(null);
  const [currentRole, setCurrentRole] = useState<Role.Item | null>(null);
  const [userCheckedKeys, setUserCheckedKeys] = useState<number[] | null>(null);
  const [userExpandedKeys, setUserExpandedKeys] = useState<number[] | null>(null);

  const [form] = Form.useForm<RoleFormValues>();
  const { modal } = App.useApp();

  // 权限检查
  const canQuery = usePermission("system:role:query");
  const canAdd = usePermission("system:role:add");
  const canEdit = usePermission("system:role:edit");
  const canDelete = usePermission("system:role:delete");
  const canPerm = usePermission("system:role:perm");

  const { run: handleSearch } = useDebounceFn(
    (value: string) => {
      setKeyword(value);
      setPagination((prev) => ({ ...prev, page: 1 }));
    },
    { wait: 300 }
  );

  // ========== 使用 Service Hooks ==========

  // 角色列表
  const { data: rolePage, isLoading: loadingRoles } = useRoleList(
    { page: pagination.page, pageSize: pagination.pageSize, keyword: keyword || undefined },
    { enabled: canQuery }
  );

  // 菜单树 - 需要权限配置权限才加载
  const { data: menuTree = [] } = useMenuTree(undefined, { enabled: canPerm });

  // 角色详情 - 权限配置弹窗打开时加载
  const { data: roleDetail } = useRoleDetail(currentRole?.id ?? 0, {
    enabled: !!currentRole && permModalVisible && canPerm,
  });

  // Mutations
  const saveMutation = useRoleSave();
  const deleteMutation = useRoleDelete();

  const treeData = useMemo(() => menuTreeToTreeData(menuTree), [menuTree]);
  const allKeys = useMemo(() => getAllTreeKeys(menuTree), [menuTree]);
  const parentKeys = useMemo(() => getParentKeys(menuTree), [menuTree]);

  // 派生状态：checkedKeys
  const checkedKeys = useMemo(() => {
    if (userCheckedKeys !== null) return userCheckedKeys;
    if (!roleDetail || !permModalVisible) return [];
    return roleDetail.menuIds.filter((id) => !parentKeys.has(id));
  }, [userCheckedKeys, roleDetail, permModalVisible, parentKeys]);

  // 派生状态：expandedKeys
  const expandedKeys = useMemo(() => {
    if (userExpandedKeys !== null) return userExpandedKeys;
    if (!roleDetail || !permModalVisible) return [];
    return allKeys;
  }, [userExpandedKeys, roleDetail, permModalVisible, allKeys]);

  // ========== 权限配置保存 ==========
  const { message } = App.useApp();
  const savePermMutation = {
    mutate: async ({ id, menuIds }: { id: number; menuIds: number[] }) => {
      try {
        await roleApi.update(id, { menuIds });
        message.success("权限配置成功");
        setPermModalVisible(false);
        setCurrentRole(null);
      } catch {
        message.error("权限配置失败");
      }
    },
    isPending: false,
  };

  // ========== 事件处理 ==========

  const openCreateModal = () => {
    setEditing(null);
    setModalVisible(true);
  };

  const openEditModal = (record: Role.Item) => {
    setEditing(record);
    setModalVisible(true);
  };

  const handleModalAfterOpen = (open: boolean) => {
    if (open) {
      if (editing) {
        form.setFieldsValue({
          id: editing.id,
          name: editing.name,
          code: editing.code,
          status: editing.status,
        });
      } else {
        form.resetFields();
        form.setFieldsValue({ status: "enabled" });
      }
      return;
    }

    form.resetFields();
    setEditing(null);
  };

  const openPermModal = (record: Role.Item) => {
    setCurrentRole(record);
    setUserCheckedKeys(null);
    setUserExpandedKeys(null);
    setPermModalVisible(true);
  };

  const onDelete = (record: Role.Item) => {
    modal.confirm({
      title: `确认删除角色「${record.name}」吗？`,
      content: "删除后不可恢复",
      onOk: () => deleteMutation.mutate(record.id),
    });
  };

  const onFinish = (values: RoleFormValues) => {
    const parsed = validateWithZod(form, roleFormSchema, values);
    if (!parsed) return;

    saveMutation.mutate(parsed, {
      onSuccess: () => {
        setModalVisible(false);
      },
    });
  };

  const handleTableChange = (paginationConfig: TablePaginationConfig) => {
    setPagination({
      page: paginationConfig.current || 1,
      pageSize: paginationConfig.pageSize || 10,
    });
  };

  const onTreeCheck: TreeProps["onCheck"] = (checked) => {
    setUserCheckedKeys(checked as number[]);
  };

  const handleSavePerm = () => {
    if (!currentRole) return;

    const menuIds = new Set<number>(checkedKeys);

    const addParents = (id: number) => {
      const findParent = (nodes: Menu.TreeItem[], targetId: number): number | null => {
        for (const node of nodes) {
          if (node.children?.some((c) => c.id === targetId)) {
            return node.id;
          }
          if (node.children) {
            const found = findParent(node.children, targetId);
            if (found) return found;
          }
        }
        return null;
      };

      const parentId = findParent(menuTree, id);
      if (parentId && !menuIds.has(parentId)) {
        menuIds.add(parentId);
        addParents(parentId);
      }
    };

    checkedKeys.forEach(addParents);

    savePermMutation.mutate({
      id: currentRole.id,
      menuIds: Array.from(menuIds),
    });
  };

  const handleSelectAll = () => {
    const leafKeys = allKeys.filter((k) => !parentKeys.has(k));
    if (checkedKeys.length === leafKeys.length) {
      setUserCheckedKeys([]);
    } else {
      setUserCheckedKeys(leafKeys);
    }
  };

  const handleExpandAll = () => {
    if (expandedKeys.length === allKeys.length) {
      setUserExpandedKeys([]);
    } else {
      setUserExpandedKeys(allKeys);
    }
  };

  // ========== 渲染 ==========

  // 无查询权限时显示提示
  if (!canQuery) {
    return (
      <PageContainer>
        <Result status="403" title="无权限" subTitle="您没有查询角色列表的权限，请联系管理员" />
      </PageContainer>
    );
  }

  const columns: ColumnsType<Role.Item> = [
    { title: "角色名称", dataIndex: "name", width: 150 },
    { title: "角色编码", dataIndex: "code", width: 150 },
    {
      title: "状态",
      dataIndex: "status",
      width: 100,
      render: (v: Role.Status) => <StatusTag status={v} />,
    },
    {
      title: "权限数量",
      dataIndex: "menuIds",
      width: 100,
      render: (ids: number[]) => <Tag color="blue">{ids?.length || 0} 个</Tag>,
    },
    {
      title: "操作",
      key: "actions",
      width: 240,
      render: (_, record) => {
        const isSuperadmin = record.code === "superadmin";
        return (
          <Space>
            {canPerm && (
              <Button type="link" onClick={() => openPermModal(record)} disabled={isSuperadmin}>
                权限配置
              </Button>
            )}
            {canEdit && (
              <Button type="link" onClick={() => openEditModal(record)} disabled={isSuperadmin}>
                编辑
              </Button>
            )}
            {canDelete && (
              <Button type="link" danger onClick={() => onDelete(record)} disabled={isSuperadmin}>
                删除
              </Button>
            )}
          </Space>
        );
      },
    },
  ];

  return (
    <PageContainer
      header={
        <div className="flex items-center justify-between">
          <h3 className="text-base font-medium m-0">角色管理</h3>
          <Space>
            <Search
              allowClear
              placeholder="角色名称 / 编码"
              onChange={(e) => handleSearch(e.target.value)}
              style={{ width: 220 }}
            />
            {canAdd && (
              <Button type="primary" onClick={openCreateModal}>
                新建角色
              </Button>
            )}
          </Space>
        </div>
      }
    >
      <Table<Role.Item>
        rowKey="id"
        columns={columns}
        dataSource={rolePage?.list || []}
        loading={loadingRoles}
        pagination={{
          current: pagination.page,
          pageSize: pagination.pageSize,
          total: rolePage?.total || 0,
          showSizeChanger: true,
          showTotal: (total) => `共 ${total} 条`,
        }}
        onChange={handleTableChange}
        size="middle"
        sticky
      />

      {/* 新建/编辑角色弹窗 */}
      <Modal
        open={modalVisible}
        title={editing ? "编辑角色" : "新建角色"}
        onCancel={() => {
          setModalVisible(false);
        }}
        onOk={() => form.submit()}
        confirmLoading={saveMutation.isPending}
        afterOpenChange={handleModalAfterOpen}
        destroyOnHidden
        width={480}
      >
        <Form<RoleFormValues> form={form} layout="vertical" onFinish={onFinish}>
          <Form.Item name="id" hidden>
            <Input />
          </Form.Item>
          <Form.Item label="角色名称" name="name" required>
            <Input placeholder="例如：系统管理员" />
          </Form.Item>
          <Form.Item label="角色编码" name="code" required>
            <Input placeholder="例如：admin" disabled={editing?.code === "superadmin"} />
          </Form.Item>
          <Form.Item label="状态" name="status" required>
            <Select>
              <Select.Option value="enabled">启用</Select.Option>
              <Select.Option value="disabled">禁用</Select.Option>
            </Select>
          </Form.Item>
        </Form>
      </Modal>

      {/* 权限配置弹窗 */}
      <Modal
        open={permModalVisible}
        title={`权限配置 - ${currentRole?.name || ""}`}
        onCancel={() => {
          setPermModalVisible(false);
          setCurrentRole(null);
        }}
        onOk={handleSavePerm}
        confirmLoading={savePermMutation.isPending}
        destroyOnHidden
        width={600}
      >
        <div style={{ marginBottom: 16 }}>
          <Space>
            <Button size="small" onClick={handleSelectAll}>
              {checkedKeys.length === allKeys.length - parentKeys.size ? "取消全选" : "全选"}
            </Button>
            <Button size="small" onClick={handleExpandAll}>
              {expandedKeys.length === allKeys.length ? "折叠全部" : "展开全部"}
            </Button>
          </Space>
        </div>
        <div
          style={{
            maxHeight: 400,
            overflow: "auto",
            border: "1px solid #f0f0f0",
            borderRadius: 4,
            padding: 12,
          }}
        >
          {treeData.length > 0 ? (
            <Tree
              checkable
              checkStrictly={false}
              checkedKeys={checkedKeys}
              expandedKeys={expandedKeys}
              onExpand={(keys) => setUserExpandedKeys(keys as number[])}
              onCheck={onTreeCheck}
              treeData={treeData}
            />
          ) : (
            <div style={{ textAlign: "center", color: "#999", padding: 40 }}>暂无菜单数据</div>
          )}
        </div>
      </Modal>
    </PageContainer>
  );
};

export default SystemRolePage;
