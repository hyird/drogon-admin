#pragma once

#include <drogon/HttpFilter.h>
#include "common/utils/ETagGenerator.hpp"

using namespace drogon;

/**
 * @brief ETag 过滤器 - 为GET请求添加ETag支持
 * @details 为成功的GET请求生成ETag，支持条件请求 (If-None-Match)
 */
class ETagFilter : public HttpFilter<ETagFilter> {
public:
    void doFilter(const HttpRequestPtr& req,
                  FilterCallback&& fcb,
                  FilterChainCallback&& fccb) override {
        // 继续处理请求
        fccb();

        // 仅对 GET 请求处理（在后置处理中完成）
        // 实际的 ETag 处理在 PostHandling 中完成
    }

    /**
     * @brief 后置处理 - 为响应添加ETag
     */
    static void processResponse(const HttpRequestPtr& req, const HttpResponsePtr& resp) {
        // ETag 支持：仅对 GET 请求的成功响应处理
        if (req->method() == Get && resp->statusCode() == k200OK && !resp->body().empty()) {
            std::string etag = ETagGenerator::generate(std::string(resp->body()));
            resp->addHeader("ETag", etag);

            // 检查 If-None-Match
            std::string ifNoneMatch = req->getHeader("If-None-Match");
            if (!ifNoneMatch.empty() && ifNoneMatch == etag) {
                resp->setStatusCode(k304NotModified);
                resp->setBody("");
            }
        }
    }
};
