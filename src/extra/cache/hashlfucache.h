#pragma once

#include <thread>
#include <cmath>
#include <vector>

#include "lfucache.h"

namespace meha
{

template<typename Key, typename Value, typename Hash = std::hash<Key>>
class HashLFUCache : public CachePolicy<Key, Value>
{
public:
    using CacheSlice = LFUCache<Key, Value>;
    explicit HashLFUCache(size_t capacity, size_t sliceNum, size_t maxAvgNum = 10)
        : LFUCache<Key, Value>(capacity)
        , m_sliceNum(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
    {
        size_t sliceSize = std::ceil(capacity * 1.0 / m_sliceNum);
        for (size_t i = 0; i < m_sliceNum; ++i) {
            m_lfuSlices.emplace_back(new CacheSlice(sliceSize));
        }
    }

    void put(const Key& key, const Value& value) override
    {
        auto sliceIdx = Hash()(key);
        m_lfuSlices[sliceIdx % m_sliceNum].put(key, value);
    }

    std::optional<Value> get(const Key& key) override
    {
        auto sliceIdx = Hash()(key);
        return m_lfuSlices[sliceIdx % m_sliceNum].get(key);
    }

    void drop(const Key& key) override
    {
        auto sliceIdx = Hash()(key);
        m_lfuSlices[sliceIdx % m_sliceNum].drop(key);
    }

    void purge() override
    {
        for (auto& slice : m_lfuSlices) {
            slice->purge();
        }
    }

    bool isFull() const override
    {
        for (const auto& slice : m_lfuSlices) {
            if (!slice->isFull()) {
                return false;
            }
        }
        return true;
    }

private:
    size_t m_sliceNum;
    std::vector<std::unique_ptr<CacheSlice>> m_lfuSlices;
};

}