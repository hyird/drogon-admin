#pragma once

#include <string>
#include <functional>

/**
 * @brief ETag 生成器 - 基于内容哈希生成 ETag
 */
class ETagGenerator {
public:
    /**
     * @brief 根据内容生成 ETag
     * @param content 响应内容
     * @return ETag 字符串 (带引号)
     */
    static std::string generate(const std::string& content) {
        auto hash = std::hash<std::string>{}(content);
        return "\"" + std::to_string(hash) + "\"";
    }
};
