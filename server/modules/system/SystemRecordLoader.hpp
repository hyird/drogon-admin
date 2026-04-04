#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <drogon/drogon.h>

namespace SystemRecordLoader {

template <typename Record, typename CacheGetter, typename DbLoader, typename CacheSetter>
drogon::Task<std::vector<Record>> loadCachedRecords(CacheGetter&& cacheGetter,
                                                    DbLoader&& dbLoader,
                                                    CacheSetter&& cacheSetter) {
    auto cached = co_await cacheGetter();
    if (cached) {
        co_return *cached;
    }

    auto records = co_await dbLoader();
    co_await cacheSetter(records);
    co_return records;
}

template <typename Record, typename CacheGetter, typename CacheFinder, typename DbLoader>
drogon::Task<std::optional<Record>> loadCachedRecord(CacheGetter&& cacheGetter,
                                                     CacheFinder&& cacheFinder,
                                                     DbLoader&& dbLoader) {
    auto cached = co_await cacheGetter();
    if (cached) {
        auto matched = cacheFinder(*cached);
        if (matched) {
            co_return matched;
        }
    }

    co_return co_await dbLoader();
}

}  // namespace SystemRecordLoader
