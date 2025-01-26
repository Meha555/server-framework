#pragma once

#include <cmath>
#include <thread>
#include <vector>
#include "lrucache.h"

namespace meha
{

template<typename Key, typename Value, typename Hash = std::hash<Key>>
class HashLRUCache : public CachePolicy<Key, Value>
{
public:
    using CacheSlice = LRUCache<Key, Value>;
    explicit HashLRUCache(size_t capacity, size_t sliceNum)
        : CachePolicy<Key, Value>(capacity)
        , m_sliceNum(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
    {
        // 计算每个分片的大小
        size_t sliceSize = std::ceil(capacity * 1.0 / m_sliceNum);
        for (size_t i = 0; i < m_sliceNum; ++i) {
            m_lruSlices.emplace_back(new CacheSlice(sliceSize));
        }
    }

    void put(const Key& key, const Value& value) override
    {
        auto sliceIdx = Hash()(key);
        m_lruSlices[sliceIdx % m_sliceNum]->put(key, value);
    }

    std::optional<Value> get(const Key& key) override
    {
        auto sliceIdx = Hash()(key);
        return m_lruSlices[sliceIdx % m_sliceNum]->get(key);
    }

    void drop(const Key& key) override
    {
        auto sliceIdx = Hash()(key);
        m_lruSlices[sliceIdx % m_sliceNum]->drop(key);
    }

    void purge() override
    {
        for (auto& slice : m_lruSlices) {
            slice->purge();
        }
    }

    bool isFull() const override
    {
        for (const auto& slice : m_lruSlices) {
            if (!slice->isFull()) {
                return false;
            }
        }
        return true;
    }

private:
    size_t m_sliceNum;
    std::vector<std::unique_ptr<CacheSlice>> m_lruSlices;
};

}