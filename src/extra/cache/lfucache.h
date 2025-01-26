#pragma once

#include <mutex>
#include <list>
#include <unordered_map>

#include "cachepolicy.h"

namespace meha
{

template<typename Key, typename Value>
class LFUCache : public CachePolicy<Key, Value>
{
public:
    using Node = CacheNode<Key, Value>;

    explicit LFUCache(size_t capacity, size_t maxAvgNum = 10)
        : CachePolicy<Key, Value>(capacity)
        , m_freqCfg{0, INT8_MAX, maxAvgNum, 0}
    {
    }

    void put(const Key& key, const Value& value) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_keyToNode.find(key);
        if (it != m_keyToNode.end())
        {
            it->second->value = value;
            touch(it->second);
            return;
        }
        tryPut(key, value);
    }

    std::optional<Value> get(const Key& key) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_keyToNode.find(key);
        if (it != m_keyToNode.end())
        {
            touch(it->second);
            return it->second->value;
        }
        return std::nullopt;
    }

    void drop(const Key& key) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_keyToNode.find(key);
		if (it == m_keyToNode.end()) {
			return;
		}
        m_keyToNode.erase(it->second->key);
        m_freqToList[it->second->freq].remove(it->second);
    }

    void purge() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_keyToNode.clear();
        m_freqToList.clear();
        m_freqCfg.totalFreq = 0;
        m_freqCfg.minFreq = INT8_MAX;
        m_freqCfg.curAvgFreq = 0;
    }

    bool isFull() const override
	{
		return m_keyToNode.size() == this->m_capacity;
	}

private:
    // 更新访问频次，重组底层数据结构
    void touch(typename Node::sptr node)
	{
        // 将节点从低访问频次的链表中删除，再添加到频次+1的链表中
        auto& oldFreqList = m_freqToList[node->freq];
        auto& newFreqList = m_freqToList[++node->freq];
        oldFreqList.remove(node);
        newFreqList.push_back(node);

        // 如果当前node的访问频次如果等于minFreq+1，并且其前驱链表为空，则说明
        // oldFreqList链表现在已经空了，需要更新最小访问频次
        if (node->freq == m_freqCfg.minFreq + 1 && m_freqToList[m_freqCfg.minFreq].empty())
            m_freqCfg.minFreq++;

        increaseFreq();
	}
    // 存入缓存，执行淘汰策略
    void tryPut(const Key& key, const Value& value)
	{
        // 1. 如果缓存已满，删除最不常访问的结点，更新当前平均访问频次和总访问频次
        if (m_keyToNode.size() == this->m_capacity) {
            auto obsolete = m_freqToList[m_freqCfg.minFreq].front();
            m_keyToNode.erase(obsolete->key);
            m_freqToList[m_freqCfg.minFreq].pop_front();
            decreaseFreq(obsolete->freq);
        }
        // 2. 创建新结点，并将其加入到对应访问频次链表尾部
        auto node = std::make_shared<Node>(key, value, 1);
        m_keyToNode[key] = node;
        m_freqToList[node->freq].push_back(node);
        // 更新最小访问频次
        if (node->freq < m_freqCfg.minFreq) {
            m_freqCfg.minFreq = node->freq;
        }
        increaseFreq();
	}
    
    // 增加总访问频次和当前平均访问频次
    void increaseFreq()
    {
        m_freqCfg.totalFreq++;
        if (m_keyToNode.empty()) {
            m_freqCfg.curAvgFreq = 0;
        } else {
            m_freqCfg.curAvgFreq = m_freqCfg.totalFreq / m_keyToNode.size();
        }
        if (m_freqCfg.curAvgFreq > m_freqCfg.maxAvgFreq) {
            handleOverMaxAvgFreq();
        }
    }

    // 减少平均访问频次和总访问频次
    void decreaseFreq(uint64_t freq)
    {
        m_freqCfg.totalFreq -= freq;
        if (m_keyToNode.empty()) {
            m_freqCfg.curAvgFreq = 0;
        } else {
            m_freqCfg.curAvgFreq = m_freqCfg.totalFreq / m_keyToNode.size();
        }
    }
    // 强制热点数据老化，防止访问频次过大，超出最大平均访问频次（计数溢出）的情况
    void handleOverMaxAvgFreq()
    {
        // 当前平均访问频次已经超过了最大平均访问频次，所有结点的访问频次 - (m_freqCfg.maxAvgFreq / 2)
        for (auto [_, node] : m_keyToNode) {
            // 所有节点的位置都要重新调整。虽然操作所有结点的时间复杂度是O(n)，但是考虑到很久才触发一次，可以忽略不计
            m_freqToList[node->freq].remove(node);
            node->freq -= m_freqCfg.maxAvgFreq / 2;
            m_freqToList[node->freq].push_back(node);
            // 更新最小访问频次
            if (node->freq < m_freqCfg.minFreq) {
                m_freqCfg.minFreq = node->freq;
            }
        }
    }

    struct FreqConfig
    {
        uint64_t totalFreq; // 当前访问缓存的总数
        uint64_t minFreq;    // 最小访问频次（用于O(1)找到最小访问频次节点）
        const uint64_t maxAvgFreq; // 最大平均访问频次
        uint64_t curAvgFreq; // 当前平均访问频次
    } m_freqCfg;
    std::unordered_map<Key, typename Node::sptr> m_keyToNode; // key到Node的映射，专门用于从链表节点获取具体数据
    // 对应访问频次的链表。不同的访问频次的数据用不同的链表存储，达到识别访问频次的目的
    // 每个链表中，链表头为该频次下最近最少访问的节点，链表尾为该频次下最近访问的节点。FIFO
    std::unordered_map<uint64_t, std::list<typename Node::sptr>> m_freqToList;
    mutable std::mutex m_mutex;
};

}