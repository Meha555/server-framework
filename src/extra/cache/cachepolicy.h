#pragma once

#include <optional>
#include <memory>
#include <cassert>

namespace meha
{

template<typename Key, typename Value>
class CachePolicy
{
public:
	explicit CachePolicy(size_t capacity)
		: m_capacity(capacity)
	{
		assert(capacity > 0);
	}
	virtual ~CachePolicy() = default;

	virtual void put(const Key& key, const Value& value) = 0;
	virtual std::optional<Value> get(const Key& key) = 0;

	virtual void drop(const Key& key) = 0;
	virtual void purge() = 0;
	virtual bool isFull() const = 0;

protected:
	size_t m_capacity;
};

template<typename Key, typename Value>
struct CacheNode
{
	using sptr = std::shared_ptr<CacheNode<Key, Value>>;
	Key key;
	Value value;
	uint64_t freq; // 访问频次（仅LFU和ARC算法使用）
	explicit CacheNode(const Key& key, const Value& value, size_t freq = 1)
		: key(key), value(value), freq(freq)
	{}
};

}
