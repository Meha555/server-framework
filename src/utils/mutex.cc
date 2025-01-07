#include "mutex.h"

namespace meha
{

Mutex::Mutex()
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); // 同线程可重入（多次上锁而不死锁）
    pthread_mutex_init(&m_mutex, &attr);
}

Mutex::~Mutex()
{
    pthread_mutex_destroy(&m_mutex);
}

int Mutex::lock() noexcept
{
    return pthread_mutex_lock(&m_mutex);
}
int Mutex::unLock() noexcept
{
    return pthread_mutex_unlock(&m_mutex);
}

RWMutex::RWMutex()
{
    pthread_rwlock_init(&m_lock, nullptr);
}

RWMutex::~RWMutex()
{
    pthread_rwlock_destroy(&m_lock);
}

int RWMutex::readLock() noexcept
{
    return pthread_rwlock_rdlock(&m_lock);
}

int RWMutex::writeLock() noexcept
{
    return pthread_rwlock_wrlock(&m_lock);
}

int RWMutex::unLock() noexcept
{
    return pthread_rwlock_unlock(&m_lock);
}

SpinLock::SpinLock()
    : m_locked(false)
{
    pthread_spin_init(&m_mutex, 0);
}
SpinLock::~SpinLock()
{
    pthread_spin_destroy(&m_mutex);
}

void SpinLock::lock() noexcept
{
    pthread_spin_lock(&m_mutex);
    m_locked = true;
}
void SpinLock::unLock() noexcept
{
    pthread_spin_unlock(&m_mutex);
    m_locked = false;
}

bool SpinLock::isLocked() const noexcept
{
    return m_locked;
}

CASLock::CASLock()
{
    m_mutex.clear();
}
void CASLock::lock() noexcept
{
    // atomic_flag_test_and_set_explicit是test_and_set成员函数对C的兼容版本
    while (std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire))
        ;
}
void CASLock::unLock() noexcept
{
    std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release);
}

}