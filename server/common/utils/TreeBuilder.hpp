#pragma once

#include <json/json.h>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

/**
 * @brief 树形结构构建工具
 */
class TreeBuilder {
public:
    template <typename T>
    struct TreeNode {
        T value;
        std::vector<TreeNode<T>> children;
    };

    template <typename T, typename GetId, typename GetParentId>
    static std::vector<TreeNode<T>> build(const std::vector<T>& items,
                                          GetId getId,
                                          GetParentId getParentId) {
        if (items.empty()) {
            return {};
        }

        std::unordered_map<int, const T*> itemMap;
        std::unordered_map<int, std::vector<const T*>> childrenByParent;
        std::vector<const T*> roots;
        itemMap.reserve(items.size());
        childrenByParent.reserve(items.size());
        roots.reserve(items.size());

        for (const auto& item : items) {
            itemMap[getId(item)] = &item;
        }

        for (const auto& item : items) {
            const int parentId = getParentId(item);
            if (parentId == 0 || itemMap.find(parentId) == itemMap.end()) {
                roots.push_back(&item);
            } else {
                childrenByParent[parentId].push_back(&item);
            }
        }

        std::function<TreeNode<T>(const T*)> buildNode;
        buildNode = [&childrenByParent, &getId, &buildNode](const T* item) -> TreeNode<T> {
            TreeNode<T> node;
            node.value = *item;

            const int id = getId(*item);
            auto childrenIt = childrenByParent.find(id);
            if (childrenIt != childrenByParent.end()) {
                node.children.reserve(childrenIt->second.size());
                for (const T* child : childrenIt->second) {
                    node.children.push_back(buildNode(child));
                }
            }

            return node;
        };

        std::vector<TreeNode<T>> result;
        result.reserve(roots.size());
        for (const T* root : roots) {
            result.push_back(buildNode(root));
        }

        return result;
    }

    template <typename T, typename GetOrder>
    static void sort(std::vector<TreeNode<T>>& tree,
                     GetOrder getOrder,
                     bool ascending = true) {
        std::sort(tree.begin(), tree.end(),
            [&getOrder, ascending](const TreeNode<T>& a, const TreeNode<T>& b) {
                int valA = getOrder(a.value);
                int valB = getOrder(b.value);
                return ascending ? (valA < valB) : (valA > valB);
            });

        for (auto& node : tree) {
            sort(node.children, getOrder, ascending);
        }
    }

    template <typename T, typename ToJson>
    static Json::Value toJson(const std::vector<TreeNode<T>>& tree,
                              ToJson convert,
                              const std::string& childrenField = "children") {
        Json::Value result(Json::arrayValue);
        for (const auto& node : tree) {
            Json::Value json = convert(node.value);
            if (!node.children.empty()) {
                json[childrenField] = toJson(node.children, convert, childrenField);
            }
            result.append(json);
        }
        return result;
    }
};
