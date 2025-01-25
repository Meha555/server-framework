#pragma once

#include "macro.h"
#include <deque>
#include <optional>

#include "cond.h"
#include "mutex.h"

namespace meha::utils
{

template<typename Data, typename Queue = std::deque<Data>>
class MtQueue
{
    MEHA_PTR_INSIDE_CLASS(MtQueue)
public:
    explicit MtQueue(size_t capacity = -1)
        : m_capacity(capacity)
    {
    }

    void push(Data data, bool instantly = false)
    {
        ScopedLock lock(&m_mutex);
        m_condEmpty.wait([this]() {
            return m_queue.size() < m_capacity;
        });
        if (instantly)
            m_queue.push_front(std::move(data));
        else
            m_queue.push_back(std::move(data));
        m_condFull.signal();
    }

    bool tryPush(Data data, bool instantly = false)
    {
        ScopedLock lock(&m_mutex);
        if (m_queue.size() >= m_capacity)
            return false;
        if (instantly)
            m_queue.push_front(std::move(data));
        else
            m_queue.push_back(std::move(data));
        m_condFull.signal();
        return true;
    }

    bool tryPushTimeWait(Data data, time_t sec, bool instantly = false)
    {
        ScopedLock lock(&m_mutex);
        if (!m_condFull.timeWait(sec, [this] {
                return m_queue.size() < m_capacity;
            })) {
            return false;
        }
        if (instantly)
            m_queue.push_front(std::move(data));
        else
            m_queue.push_back(std::move(data));
        m_condFull.signal();
        return true;
    }

    Data pop()
    {
        ScopedLock lock(&m_mutex);
        m_condFull.wait([this] {
            return !m_queue.empty();
        });
        Data data = std::move(m_queue.front());
        m_queue.pop_front();
        m_condEmpty.signal();
        return data;
    }

    std::optional<Data> tryPop()
    {
        ScopedLock lock(&m_mutex);
        if (m_queue.empty())
            return std::nullopt;
        Data data = std::move(m_queue.front());
        m_queue.pop_front();
        m_condEmpty.signal();
        return data;
    }

    std::optional<Data> tryPopTimeWait(time_t sec)
    {
        ScopedLock lock(&m_mutex);
        if (!m_condFull.timeWait(sec, [this] {
                return !m_queue.empty();
            })) {
            return std::nullopt;
        }
        Data data = std::move(m_queue.front());
        m_queue.pop_front();
        m_condEmpty.signal();
        return data;
    }

    size_t capacity() const
    {
        return m_capacity;
    }

private:
    const size_t m_capacity; // -1 表示无限制。超过容量时，会阻塞等待
    Queue m_queue;
    Mutex m_mutex;
    ConditionVariable m_condEmpty;
    ConditionVariable m_condFull;
};

}