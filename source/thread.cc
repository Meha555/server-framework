#include "thread.h"
#include "log.h"
#include "mutex.hpp"
#include "sem.h"
#include "util.h"
#include <exception>

// FIXME - 这些底层库的失败和异常的处理，应该怎么做？是不是可以参考一下Java

namespace meha {

static Logger::ptr root_logger = GET_LOGGER("root");

/* --------------------------------- 线程局部变量 --------------------------------- */

// 当前线程的 Thread 实例的指针，如果不是我们用Thread创建线程，这个指针变量就是初值nullptr
static thread_local Thread *t_this_thread{nullptr};
static thread_local pid_t t_this_tid{0};
// 当前线程的线程名
static thread_local std::string_view t_this_tname{"annoymous_thread"};

/* ------------------------------------ 锁 ----------------------------------- */

Mutex::Mutex()
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);  //同线程可重入（多次上锁而不死锁）
    if (pthread_mutex_init(&m_mutex, &attr)) {
        LOG_FMT_FATAL(GET_ROOT_LOGGER(), "pthread_mutex_init() 失败：%s", ::strerror(errno));
    }
}
Mutex::~Mutex() { pthread_mutex_destroy(&m_mutex); }

int Mutex::lock() noexcept { return pthread_mutex_lock(&m_mutex); }
int Mutex::unlock() noexcept { return pthread_mutex_unlock(&m_mutex); }

RWLock::RWLock()
{
    if (pthread_rwlock_init(&m_lock, nullptr)) {
        LOG_FMT_FATAL(GET_ROOT_LOGGER(), "pthread_rwlock_init() 失败：%s", ::strerror(errno));
    }
}

RWLock::~RWLock()
{
    if (pthread_rwlock_destroy(&m_lock)) {
        LOG_FMT_FATAL(GET_ROOT_LOGGER(), "pthread_rwlock_destroy() 失败：%s", ::strerror(errno));
    }
}

int RWLock::readLock() noexcept
{
    int ret = pthread_rwlock_rdlock(&m_lock);
    if (ret) {
        LOG_FMT_FATAL(GET_ROOT_LOGGER(), "pthread_rwlock_rdlock() 失败：%s", ::strerror(errno));
    }
    return ret;
}

int RWLock::writeLock() noexcept
{
    int ret = pthread_rwlock_wrlock(&m_lock);
    if (ret) {
        fprintf(stderr, "pthread_rwlock_wrlock() 失败：%s", ::strerror(errno));
    }
    return ret;
}

int RWLock::unlock() noexcept
{
    int ret = pthread_rwlock_unlock(&m_lock);
    if (ret) {
        fprintf(stderr, "pthread_rwlock_unlock() 失败：%s", ::strerror(errno));
    }
    return ret;
}

SpinLock::SpinLock() { pthread_spin_init(&m_mutex, 0); }
SpinLock::~SpinLock() { pthread_spin_destroy(&m_mutex); }

void SpinLock::lock() noexcept { pthread_spin_lock(&m_mutex); }
void SpinLock::unlock() noexcept { pthread_spin_unlock(&m_mutex); }

CASLock::CASLock() { m_mutex.clear(); }
void CASLock::lock() noexcept
{
    // atomic_flag_test_and_set_explicit是test_and_set成员函数对C的兼容版本
    while (std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire))
        ;
}
void CASLock::unlock() noexcept { std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release); }

/* ----------------------------------- 线程 ----------------------------------- */

Thread::Thread(ThreadFunc callback, const std::string_view &name)
    : m_thread(0),
      m_callback(std::move(callback)),
      m_sem_sync(0),
      m_started(true),
      m_joined(false)
{
    LOG_FMT_DEBUG(GET_ROOT_LOGGER(), "创建线程%s[%d]", name.data(), GetThreadID());
    SetCurrentName(name);
    ThreadClosure *closure = new ThreadClosure(name, callback, this);
    int ret = pthread_create(&m_thread, nullptr, &Thread::Run, closure);
    if (ret) {  // 创建线程失败
        t_this_thread = nullptr;
        t_this_tid = -1;
        delete closure;
        ASSERT_FMT(ret == 0, "创建线程 %s 失败：%s", name.data(), strerror(errno));
    } else {  // 创建线程成功
              // 注意这里是在主线程中，先让主线程停住，因为要为子线程绑定ID、设置名字之类，且还没那么快开跑任务worker，所以需要同步一下
              // 我采用的是thread_local变量来存储的，因此需要在子线程来设置
        m_sem_sync.wait();
    }
}

Thread::~Thread()
{
    LOG_FMT_DEBUG(GET_ROOT_LOGGER(), "分离线程%s[%d]", GetCurrentName().data(), GetThreadID());
    // 而我们不希望阻塞主线程，因此这里用detach
    // 如果线程有效且不为join，将线程与主线程分离
    if (m_started && !m_joined) {
        pthread_detach(m_thread);
    }
}

Thread *Thread::GetCurrent() { return t_this_thread; }

pid_t Thread::GetCurrentId() { return t_this_tid; }

const std::string_view &Thread::GetCurrentName() { return t_this_tname; }

void Thread::SetCurrentName(const std::string_view &name) { t_this_tname = name; }

void Thread::join()
{
    assert(m_started);
    assert(!m_joined);
    m_joined = true;
    int ret = pthread_join(m_thread, nullptr);
    if (ret) {
        LOG_FMT_FATAL(root_logger,
                      "pthread_join() 等待线程 %s[%d] 失败：%s",
                      GetCurrentName().data(),
                      GetCurrentId(),
                      ::strerror(errno));
        throw std::system_error();
    }
}

void *Thread::Run(void *args)
{
    // 这个函数就是在子线程中
    auto closure = static_cast<ThreadClosure *>(args);
    closure->runInThread();
    delete closure;
    return EXIT_SUCCESS;
}

void Thread::ThreadClosure::runInThread()
{
    t_this_thread = static_cast<Thread *>(user_data);
    t_this_tid = GetThreadID();
    t_this_tname = name;
    pthread_setname_np(t_this_thread->m_thread, t_this_tname.substr(0, 15).data());

    // REVIEW 这里针对线程函数使用swap，目的是防止线程函数中使用了智能指针导致的一些引用计数问题
    ThreadFunc worker;
    worker.swap(t_this_thread->m_callback);

    try {
        t_this_thread->m_sem_sync.post();  // 启动被暂停的主线程，开始并发执行
        // std::function的swap函数是移动语义的实现之一，这里存在移后源对象不能使用的问题
        worker();  // 执行工作函数 可以试试用t_this_thread->m_callback();
    } catch (const std::exception &e) {
        LOG_FMT_FATAL(root_logger, "线程 %s[%d] 执行异常, 原因：%s", t_this_tname.data(), t_this_tid, e.what());
        ::abort();
    }
}

}  // namespace meha