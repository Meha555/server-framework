#ifndef SERVER_FRAMEWORK_MUTEX_H
#define SERVER_FRAMEWORK_MUTEX_H

// #include "log.h"
#include "noncopyable.h"
#include "util.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <unistd.h>

namespace meha {
/**
 * @brief 对 semaphore.h 信号量的简单封装
 */
class Semaphore {
    DISABLE_COPY_MOVE(Semaphore)
public:
    explicit Semaphore(uint32_t count);
    ~Semaphore();
    // -1，值为零时阻塞
    void wait();
    // +1
    void post();

private:
    sem_t m_semaphore;
};

/**
 * @brief pthread 互斥量的封装
 * @note 这里我设置为了可重入锁
 */
class Mutex {
    DISABLE_COPY_MOVE(Mutex)
public:
    Mutex()
    {
        fprintf(stderr, "Mutex创建 %s, %s, %d\n", __PRETTY_FUNCTION__, __FILE__, __LINE__);
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);  //同线程可重入（多次上锁而不死锁）
        pthread_mutex_init(&m_mutex, &attr);
    }

    ~Mutex() { pthread_mutex_destroy(&m_mutex); }

    int lock() { return pthread_mutex_lock(&m_mutex); }

    int unlock() { return pthread_mutex_unlock(&m_mutex); }

private:
    pthread_mutex_t m_mutex{};
};

/**
 * @brief pthread 读写锁的封装
 * @details 读写锁是一对互斥锁，分为读锁和写锁。
 * 读锁和写锁互斥，让一个线程在进行读操作时，不允许其他线程的写操作，但是不影响其他线程的读操作。
 * 当一个线程在进行写操作时，不允许任何线程进行读操作或者写操作。
 */
class RWLock {
    DISABLE_COPY_MOVE(RWLock)
public:
    RWLock()
    {
        if (pthread_rwlock_init(&m_lock, nullptr)) {
            fprintf(stderr, "pthread_rwlock_init() 失败：%s", ::strerror(errno));
            // LOG_FMT_FATAL(GET_ROOT_LOGGER(), "pthread_rwlock_init() 失败：%s", ::strerror(errno));
        }
    }

    ~RWLock()
    {
        if (pthread_rwlock_destroy(&m_lock)) {
            fprintf(stderr, "pthread_rwlock_destroy() 失败：%s", ::strerror(errno));
            // LOG_FMT_FATAL(GET_ROOT_LOGGER(), "pthread_rwlock_destroy() 失败：%s", ::strerror(errno));
        }
    }

    int readLock()
    {
        int ret = pthread_rwlock_rdlock(&m_lock);
        if (ret) {
            fprintf(stderr, "pthread_rwlock_rdlock() 失败：%s", ::strerror(errno));
            // LOG_FMT_FATAL(GET_ROOT_LOGGER(), "pthread_rwlock_rdlock() 失败：%s", ::strerror(errno));
        }
        return ret;
    }

    int writeLock()
    {
        int ret = pthread_rwlock_wrlock(&m_lock);
        if (ret) {
            fprintf(stderr, "pthread_rwlock_wrlock() 失败：%s", ::strerror(errno));
            // LOG_FMT_FATAL(GET_ROOT_LOGGER(), "pthread_rwlock_wrlock() 失败：%s", ::strerror(errno));
        }
        return ret;
    }

    int unlock()
    {
        int ret = pthread_rwlock_unlock(&m_lock);
        if (ret) {
            fprintf(stderr, "pthread_rwlock_unlock() 失败：%s", ::strerror(errno));
            // LOG_FMT_FATAL(GET_ROOT_LOGGER(), "pthread_rwlock_unlock() 失败：%s", ::strerror(errno));
        }
        return ret;
    }

private:
    pthread_rwlock_t m_lock{};
};

/**
 * @brief 作用域线程锁包装器
 * Mutex 需要实现 lock() 与 unlock() 方法
 */
template <typename Mutex>
class ScopedLockImpl {
    DISABLE_COPY_MOVE(ScopedLockImpl)
public:
    explicit ScopedLockImpl(Mutex *mutex) : m_mutex(mutex), m_locked(true) { m_mutex->lock(); }

    ~ScopedLockImpl() { unlock(); }

    void lock()
    {
        // 避免重复加锁（这里不是可重入的意思）//FIXME 这里不需要加锁保护使之成为原子性的吗？这样难道不会有线程安全问题?
        if (!m_locked) {
            m_mutex->lock();
            m_locked = true;
        }
    }

    void unlock()
    {
        // 避免重复解锁
        if (m_locked) {  // FIXME - 这里也是同理线程安全问题，下面还有很多
            m_locked = false;
            m_mutex->unlock();
        }
    }

private:
    Mutex *m_mutex;
    bool m_locked;
};

/**
 * @brief 作用域读锁包装器
 * RWLock 需要实现 readLock() 与 unlock() 方法
 */
template <typename RWLock>
class ReadScopedLockImpl {
    DISABLE_COPY_MOVE(ReadScopedLockImpl)
public:
    explicit ReadScopedLockImpl(RWLock *mutex) : m_mutex(mutex), m_locked(true) { m_mutex->readLock(); }

    ~ReadScopedLockImpl() { unlock(); }

    void lock()
    {
        if (!m_locked) {
            m_mutex->readLock();
            m_locked = true;
        }
    }

    void unlock()
    {
        if (m_locked) {
            m_locked = false;
            m_mutex->unlock();
        }
    }

private:
    RWLock *m_mutex;
    bool m_locked;
};

/**
 * @brief 作用域读写锁包装器
 * RWLock 需要实现 writeLock() 与 unlock() 方法
 */
template <typename RWLock>
class WriteScopedLockImpl {
    DISABLE_COPY_MOVE(WriteScopedLockImpl)
public:
    explicit WriteScopedLockImpl(RWLock *mutex) : m_mutex(mutex), m_locked(true) { m_mutex->writeLock(); }

    ~WriteScopedLockImpl() { unlock(); }

    void lock()
    {
        if (!m_locked) {
            m_mutex->writeLock();
            m_locked = true;
        }
    }

    void unlock()
    {
        if (m_locked) {
            m_locked = false;
            m_mutex->unlock();
        }
    }

private:
    RWLock *m_mutex;
    bool m_locked;
};

/**
 * @brief 互斥锁的 RAII
 */
using ScopedLock = ScopedLockImpl<Mutex>;

/**
 * @brief 读写锁针对读操作的作用域 RAII 实现
 */
using ReadScopedLock = ReadScopedLockImpl<RWLock>;

/**
 * @brief 读写锁针对写操作的作用域 RAII 实现
 */
using WriteScopedLock = WriteScopedLockImpl<RWLock>;
}  // namespace meha

#endif  // MUTEX_)