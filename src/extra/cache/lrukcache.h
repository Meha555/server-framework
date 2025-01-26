#pragma once

#include "lrucache.h"

namespace meha
{

template<typename Key, typename Value>
class LRUKCache : public LRUCache<Key, Value>
{
public:
    explicit LRUKCache(size_t capacity, size_t historyCapacity = 4, size_t k = 2)
        : LRUCache<Key, Value>(capacity)
        , m_k(k)
        , m_hisitoryList(historyCapacity)
    {
    }

    // 只在访问了k次后才会将数据缓存
    void put(const Key& key, const Value& value) override
    {
        // 1. 更新历史访问队列(虽然put操作不属于访问值，但如果不更新访问次数，在从来没有get的情况下就无法插入新数据）
        size_t count = updateHistory(key);
        // 2. 先检查缓存队列是否命中：命中则更新，否则按照k的策略淘汰缓存数据再插入
        if (LRUCache<Key, Value>::get(key)) {
            LRUCache<Key, Value>::put(key, value);
            return;
        }
        // 优化体验（缓存没满时直接插入缓存队列）
        if (!this->isFull()) {
            LRUCache<Key, Value>::put(key, value);
        }
        // 3. 淘汰缓存数据
        else if (count >= m_k) {
            m_hisitoryList.drop(key); // 删除这个不再需要的历史访问记录
            LRUCache<Key, Value>::put(key, value); // 将该条记录放入缓存队列
        }
    }

    // 每次调用要累加对应key的访问次数
    std::optional<Value> get(const Key& key) override
    {
        // 1. 首先在访问历史队列中累加访问次数
        updateHistory(key);
        // 2. 返回缓存队列中数据（可能没有值）
        return LRUCache<Key, Value>::get(key); // 注意get操作不会淘汰缓存数据，只有put操作会。因此检查historyList的操作应该放在put操作中
    }

private:
    size_t updateHistory(const Key& key)
    {
        size_t count = m_hisitoryList.get(key).value_or(0);
        m_hisitoryList.put(key, ++count);
        return count;
    }

    size_t m_k;// 进入缓存队列的访问次数阈值
    LRUCache<Key, size_t> m_hisitoryList;// 访问数据历史记录(value为访问次数)
};

}