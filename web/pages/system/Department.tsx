import { useState, useMemo } from "react";
import {
  Button,
  Form,
  Input,
  Modal,
  Select,
  Space,
  Table,
  TreeSelect,
  InputNumber,
  App,
  Result,
  type TreeSelectProps,
} from "antd";
import type { ColumnsType } from "antd/es/table";
import { PlusOutlined } from "@ant-design/icons";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useDebounceFn } from "ahooks";
import type { Department, User } from "@/types";
import { departmentApi, userApi } from "@/services";
import { usePermission } from "@/hooks";
import { StatusTag } from "@/components/StatusTag";
import { PageContainer } from "@/components/PageContainer";
import { departmentFormSchema, validateWithZod, type DepartmentFormValues } from "@/validation";
import { normalizeNullablePositiveId } from "@/utils";

const { Search } = Input;

function filterDepartmentTree(tree: Department.TreeItem[], keyword: string): Department.TreeItem[] {
  const kw = keyword.trim().toLowerCase();
  if (!kw) return tree;

  const filter = (nodes: Department.TreeItem[]): Department.TreeItem[] => {
    const result: Department.TreeItem[] = [];
    nodes.forEach((n) => {
      const selfMatch =
        n.name.toLowerCase().includes(kw) || (n.code || "").toLowerCase().includes(kw);
      const children = n.children ? filter(n.children) : [];
      if (selfMatch || children.length > 0) {
        const node: Department.TreeItem = { ...n };
        if (children.length > 0) {
          node.children = children;
        } else {
          delete node.children;
        }
        result.push(node);
      }
    });
    return result;
  };
  return filter(tree);
}

const SystemDepartmentPage = () => {
  const [keyword, setKeyword] = useState("");
  const [modalVisible, setModalVisible] = useState(false);
  const [editing, setEditing] = useState<Department.TreeItem | null>(null);
  const [form] = Form.useForm<DepartmentFormValues>();
  const queryClient = useQueryClient();
  const { message, modal } = App.useApp();

  // 权限检查
  const canQuery = usePermission("system:dept:query");
  const canAdd = usePermission("system:dept:add");
  const canEdit = usePermission("system:dept:edit");
  const canDelete = usePermission("system:dept:delete");

  const { run: handleSearch } = useDebounceFn((value: string) => setKeyword(value), { wait: 300 });

  // 部门树 - 需要查询权限
  const { data: rawDepartmentTree = [], isLoading } = useQuery({
    queryKey: ["departments", "tree", { all: true }],
    queryFn: () => departmentApi.getTree(),
    enabled: canQuery,
  });

  // 用户列表 - 用于负责人选择
  const { data: usersData } = useQuery({
    queryKey: ["users", "all"],
    queryFn: () => userApi.getList({}),
    enabled: canQuery,
  });
  const userList = useMemo(() => usersData?.list || [], [usersData?.list]);
  const userMap = useMemo(() => {
    const map = new Map<number, User.Item>();
    userList.forEach((u) => map.set(u.id, u));
    return map;
  }, [userList]);

  const departmentTree = useMemo(
    () => filterDepartmentTree(rawDepartmentTree, keyword),
    [rawDepartmentTree, keyword]
  );

  const getParentTreeData = (excludeId?: number): TreeSelectProps["treeData"] => {
    const loop = (nodes: Department.TreeItem[]): TreeSelectProps["treeData"] =>
      nodes
        .filter((n) => n.id !== excludeId)
        .map((n) => ({
          title: n.name,
          value: n.id,
          children: n.children ? loop(n.children) : undefined,
        }));
    return loop(rawDepartmentTree);
  };

  const saveMutation = useMutation({
    mutationFn: async (values: DepartmentFormValues) => {
      if (values.id) {
        const payload: Department.UpdateDto = {
          name: values.name,
          code: values.code,
          parentId: values.parentId === undefined ? null : values.parentId,
          order: values.order,
          leaderId: values.leaderId,
          status: values.status,
        };
        await departmentApi.update(values.id, payload);
      } else {
        const payload: Department.CreateDto = {
          name: values.name,
          code: values.code,
          parentId: values.parentId === undefined ? null : values.parentId,
          order: values.order,
          leaderId: values.leaderId,
          status: values.status,
        };
        await departmentApi.create(payload);
      }
    },
    onSuccess: () => {
      message.success("保存成功");
      setModalVisible(false);
      queryClient.invalidateQueries({ queryKey: ["departments"] });
    },
  });

  const deleteMutation = useMutation({
    mutationFn: departmentApi.remove,
    onSuccess: () => {
      message.success("删除成功");
      queryClient.invalidateQueries({ queryKey: ["departments"] });
    },
  });

  const openCreateModal = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({
      status: "enabled",
      order: 0,
      parentId: null,
    });
    setModalVisible(true);
  };

  const openCreateChildModal = (parent: Department.TreeItem) => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({
      status: "enabled",
      order: 0,
      parentId: parent.id,
    });
    setModalVisible(true);
  };

  const openEditModal = (record: Department.TreeItem) => {
    setEditing(record);
    form.setFieldsValue({
      id: record.id,
      name: record.name,
      code: record.code,
      parentId: normalizeNullablePositiveId(record.parentId),
      order: record.order,
      leaderId: normalizeNullablePositiveId(record.leaderId),
      status: record.status,
    });
    setModalVisible(true);
  };

  const onDelete = (record: Department.TreeItem) => {
    modal.confirm({
      title: `确认删除部门「${record.name}」吗？`,
      content: "若存在子部门或用户，请先处理后再删除。",
      onOk: () => deleteMutation.mutate(record.id),
    });
  };

  const onFinish = (values: DepartmentFormValues) => {
    const parsed = validateWithZod(form, departmentFormSchema, values);
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
        <Result status="403" title="无权限" subTitle="您没有查询部门列表的权限，请联系管理员" />
      </PageContainer>
    );
  }

  const columns: ColumnsType<Department.TreeItem> = [
    {
      title: "部门名称",
      dataIndex: "name",
      width: 200,
    },
    {
      title: "部门编码",
      dataIndex: "code",
      width: 120,
      render: (v) => v || "-",
    },
    {
      title: "负责人",
      dataIndex: "leaderId",
      width: 100,
      render: (v: number | null) =>
        v ? userMap.get(v)?.nickname || userMap.get(v)?.username || "-" : "-",
    },
    {
      title: "联系电话",
      dataIndex: "leaderId",
      key: "phone",
      width: 130,
      render: (v: number | null) => (v ? userMap.get(v)?.phone || "-" : "-"),
    },
    {
      title: "邮箱",
      dataIndex: "leaderId",
      key: "email",
      width: 180,
      render: (v: number | null) => (v ? userMap.get(v)?.email || "-" : "-"),
    },
    {
      title: "排序",
      dataIndex: "order",
      width: 80,
    },
    {
      title: "状态",
      dataIndex: "status",
      width: 80,
      render: (v: Department.Status) => <StatusTag status={v} />,
    },
    {
      title: "操作",
      key: "actions",
      width: 200,
      render: (_, record) => (
        <Space>
          {canAdd && (
            <Button
              type="link"
              size="small"
              icon={<PlusOutlined />}
              onClick={() => openCreateChildModal(record)}
            >
              新增
            </Button>
          )}
          {canEdit && (
            <Button type="link" size="small" onClick={() => openEditModal(record)}>
              编辑
            </Button>
          )}
          {canDelete && (
            <Button type="link" size="small" danger onClick={() => onDelete(record)}>
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
          <h3 className="text-base font-medium m-0">部门管理</h3>
          <Space>
            <Search
              allowClear
              placeholder="部门名称 / 编码"
              onChange={(e) => handleSearch(e.target.value)}
              style={{ width: 240 }}
            />
            {canAdd && (
              <Button type="primary" onClick={openCreateModal}>
                新建部门
              </Button>
            )}
          </Space>
        </div>
      }
    >
      <Table<Department.TreeItem>
        rowKey="id"
        columns={columns}
        dataSource={departmentTree}
        loading={isLoading}
        pagination={false}
        size="middle"
        expandable={{
          defaultExpandAllRows: true,
          rowExpandable: (record) => Array.isArray(record.children) && record.children.length > 0,
        }}
        scroll={{ x: 1100 }}
        sticky
      />

      <Modal
        open={modalVisible}
        title={editing ? "编辑部门" : "新建部门"}
        onCancel={() => {
          setModalVisible(false);
        }}
        onOk={() => form.submit()}
        confirmLoading={saveMutation.isPending}
        afterOpenChange={handleModalAfterOpen}
        destroyOnHidden
        width={560}
      >
        <Form<DepartmentFormValues> form={form} layout="vertical" onFinish={onFinish}>
          <Form.Item name="id" hidden>
            <Input />
          </Form.Item>

          <Form.Item label="部门名称" name="name" required>
            <Input placeholder="请输入部门名称" />
          </Form.Item>

          <Form.Item label="部门编码" name="code">
            <Input placeholder="请输入部门编码（可选）" />
          </Form.Item>

          <Form.Item label="上级部门" name="parentId">
            <TreeSelect
              allowClear
              treeData={getParentTreeData(editing?.id)}
              placeholder="不选则为顶级部门"
              treeDefaultExpandAll
            />
          </Form.Item>

          <Form.Item label="负责人" name="leaderId">
            <Select
              allowClear
              showSearch
              placeholder="请选择负责人"
              optionFilterProp="label"
              options={userList.map((u) => ({
                value: u.id,
                label: u.nickname || u.username,
              }))}
            />
          </Form.Item>

          <Form.Item label="排序" name="order">
            <InputNumber style={{ width: "100%" }} placeholder="数值越小越靠前" />
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

export default SystemDepartmentPage;
