#pragma once

#include <atomic>
#include <pthread.h>
#include <unistd.h>

#include "utils/noncopyable.h"

namespace meha
{

/**
 * @brief pthread 互斥量的封装
 * @note 这里我设置为了可重入锁
 */
class Mutex
{
    DISABLE_COPY_MOVE(Mutex)
public:
    explicit Mutex();
    ~Mutex();

    int lock() noexcept;
    int unLock() noexcept;

private:
    pthread_mutex_t m_mutex{};
};

/**
 * @brief pthread 读写锁的封装
 * @details 读写锁是一对互斥锁，分为读锁和写锁。
 * 读锁和写锁互斥，让一个线程在进行读操作时，不允许其他线程的写操作，但是不影响其他线程的读操作。
 * 当一个线程在进行写操作时，不允许任何线程进行读操作或者写操作。
 */
class RWMutex
{
    DISABLE_COPY_MOVE(RWMutex)
public:
    explicit RWMutex();
    ~RWMutex();

    int readLock() noexcept;
    int writeLock() noexcept;
    int unLock() noexcept;

private:
    pthread_rwlock_t m_lock{};
};

/**
 * @brief 作用域线程锁包装器
 * Mutex 需要实现 lock() 与 unLock() 方法
 */
template<typename Mutex>
class ScopedLockImpl
{
    DISABLE_COPY_MOVE(ScopedLockImpl)
public:
    explicit ScopedLockImpl(Mutex *mutex)
        : m_mutex(mutex)
        , m_locked(true)
    {
        m_mutex->lock();
    }

    ~ScopedLockImpl()
    {
        unLock();
    }

    void lock() noexcept
    {
        // 避免重复加锁（这里不是可重入的意思）//FIXME 这里不需要加锁保护使之成为原子性的吗？这样难道不会有线程安全问题?
        if (!m_locked) {
            m_mutex->lock();
            m_locked = true;
        }
    }

    void unLock() noexcept
    {
        // 避免重复解锁
        if (m_locked) { // FIXME - 这里也是同理线程安全问题，下面还有很多
            m_locked = false;
            m_mutex->unLock();
        }
    }

private:
    Mutex *m_mutex;
    bool m_locked;
};

/**
 * @brief 作用域读锁包装器
 * RWMutex 需要实现 readLock() 与 unLock() 方法
 */
template<typename RWMutex>
class ReadScopedLockImpl
{
    DISABLE_COPY_MOVE(ReadScopedLockImpl)
public:
    explicit ReadScopedLockImpl(RWMutex *mutex)
        : m_mutex(mutex)
        , m_locked(true)
    {
        m_mutex->readLock();
    }

    ~ReadScopedLockImpl()
    {
        unLock();
    }

    void lock() noexcept
    {
        if (!m_locked) {
            m_mutex->readLock();
            m_locked = true;
        }
    }

    void unLock() noexcept
    {
        if (m_locked) {
            m_locked = false;
            m_mutex->unLock();
        }
    }

private:
    RWMutex *m_mutex;
    bool m_locked;
};

/**
 * @brief 作用域读写锁包装器
 * RWMutex 需要实现 writeLock() 与 unLock() 方法
 */
template<typename RWMutex>
class WriteScopedLockImpl
{
    DISABLE_COPY_MOVE(WriteScopedLockImpl)
public:
    explicit WriteScopedLockImpl(RWMutex *mutex)
        : m_mutex(mutex)
        , m_locked(true)
    {
        m_mutex->writeLock();
    }

    ~WriteScopedLockImpl()
    {
        unLock();
    }

    void lock() noexcept
    {
        if (!m_locked) {
            m_mutex->writeLock();
            m_locked = true;
        }
    }

    void unLock() noexcept
    {
        if (m_locked) {
            m_locked = false;
            m_mutex->unLock();
        }
    }

private:
    RWMutex *m_mutex;
    bool m_locked;
};

class SpinLock
{
    DISABLE_COPY_MOVE(SpinLock)
public:
    explicit SpinLock();
    ~SpinLock();
    void lock() noexcept;
    void unLock() noexcept;

    bool isLocked() const noexcept;

private:
    pthread_spinlock_t m_mutex;
    bool m_locked;
};

/**
 * @brief 原子锁
 * @details 采用自旋锁的实现
 */
class CASLock
{
    DISABLE_COPY_MOVE(CASLock)
public:
    explicit CASLock();
    ~CASLock() = default;
    void lock() noexcept;
    void unLock() noexcept;

private:
    // 原子状态
    volatile std::atomic_flag m_mutex;
};

// 互斥锁的 RAII 实现
using ScopedLock = ScopedLockImpl<Mutex>;

// 读写锁针对读操作的作用域 RAII 实现
using ReadScopedLock = ReadScopedLockImpl<RWMutex>;

// 读写锁针对写操作的作用域 RAII 实现
using WriteScopedLock = WriteScopedLockImpl<RWMutex>;

// 自旋锁的作用域 RAII 实现
using SpinScopedLock = ScopedLockImpl<SpinLock>;

} // namespace meha
