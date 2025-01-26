#pragma once

#include <memory>
#include <list>
#include <unordered_map>
#include <mutex>

#include "cachepolicy.h"

namespace meha
{

template<typename Key, typename Value>
class LRUCache : public CachePolicy<Key, Value>
{
public:
	using Node = CacheNode<Key, Value>;

	explicit LRUCache(size_t capacity)
		: CachePolicy<Key, Value>(capacity)
	{
	}

	void put(const Key& key, const Value& value) override
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		// 1. 检查是否缓存命中：命中则更新值，否则继续
		auto it = m_keyToNode.find(key);
		if (it != m_keyToNode.end()) {
			it->second->value = value; // 更新值
			touch(it->second); // 标记数据被访问(put操作也算访问)
			return;
		}
		// 2. 检查容量并放置：足够就放，不够就先淘汰一个
		tryPut(key, value);
	}

	std::optional<Value> get(const Key& key) override
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		// 直接查缓存，有就返回，没有就返回空
		auto it = m_keyToNode.find(key);
		if (it != m_keyToNode.end()) {
			touch(it->second);
			return it->second->value;
		}
		return std::nullopt;
	}

	// 只允许drop已有的缓存
	void drop(const Key& key) override
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_keyToNode.find(key);
		if (it == m_keyToNode.end()) {
			return;
		}
		m_lruList.remove(it->second);
		m_keyToNode.erase(key);
	}

	void purge() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_keyToNode.clear();
		m_lruList.clear();
    }

	bool isFull() const override
	{
		return m_keyToNode.size() == this->m_capacity;
	}

private:
	// 重组底层数据结构
	void touch(typename Node::sptr node)
	{
		// 先把节点从链表中移除，然后再放到链表尾部
		m_lruList.remove(node);
		m_lruList.push_back(node);
	}
	// 存入缓存，执行淘汰策略
	void tryPut(const Key& key, const Value& value)
	{
		// 1. 判断容器是否满
		if (m_lruList.size() == this->m_capacity) {
			// 淘汰最久未访问的节点
			m_keyToNode.erase(m_lruList.front()->key);
			m_lruList.pop_front();
		}
		// 2. 放置数据
		auto node = std::make_shared<Node>(key, value);
		m_lruList.push_back(node);
		m_keyToNode[key] = node;
	}

	std::unordered_map<Key, typename Node::sptr> m_keyToNode; // key到Node的映射，专门用于从链表节点获取具体数据
	std::list<typename Node::sptr> m_lruList; // 链表头是最久未访问的，链表尾是最近访问的，FIFO
	mutable std::mutex m_mutex;
};

}
