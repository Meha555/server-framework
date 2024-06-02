#include "thread.h"
#include "log.h"
#include "util.h"
#include <cassert>
#include <cstring>
#include <exception>

// FIXME - 这些底层库的失败和异常的处理，应该怎么做？是不是可以参考一下Java

namespace meha {

#define DEFAULT_THREAD_NAME "UNNAMED_THREAD"

/**
 * 线程局部变量
 */
// 记录当前线程的 Thread 实例的指针，如果不是我们用Thread创还能的线程，这个指针变量就是初值nullptr
static thread_local Thread *s_this_thread = nullptr;
static thread_local pid_t s_this_tid = 0;
// 记录当前线程的线程名
static thread_local std::string s_this_tname = DEFAULT_THREAD_NAME;

static Logger::ptr root_logger = GET_LOGGER("root");

Semaphore::Semaphore(uint32_t count)
{
    if (sem_init(&m_semaphore, 0, count)) {
        LOG_FMT_FATAL(root_logger, "sem_init() 初始化信号量失败：%s", ::strerror(errno));
        throw std::system_error();  // FIXME 有观点认为构造函数最好不要抛出异常
    }
}

Semaphore::~Semaphore()
{
    if (sem_destroy(&m_semaphore)) {
        LOG_FMT_FATAL(root_logger, "sem_destroy() 销毁信号量失败：%s", ::strerror(errno));
    }
}

void Semaphore::wait()
{
    if (sem_wait(&m_semaphore)) {
        LOG_FMT_FATAL(root_logger, "sem_wait() 异常：%s", ::strerror(errno));
        throw std::system_error();
        // TODO 失败时是否应该直接结束程序？
    }
}

void Semaphore::post()
{
    if (sem_post(&m_semaphore)) {
        LOG_FMT_FATAL(root_logger, "sem_post() 异常：%s", ::strerror(errno));
        throw std::system_error();
    }
}

Thread::Thread(ThreadFunc callback, const std::string &name)
    : m_tname(name), m_thread(0), m_callback(std::move(callback)), m_sem_sync(0), m_started(true), m_joined(false)
{
    SetThisName(name);
    int ret = pthread_create(&m_thread, nullptr, &Thread::Run, this);
    if (ret) {  // 创建线程失败
        s_this_thread = nullptr;
        s_this_tid = -1;
        s_this_tname = DEFAULT_THREAD_NAME;
        LOG_FMT_FATAL(root_logger, "pthread_create() 创建线程 %s 失败：%s", name.c_str(), ::strerror(errno));
        throw std::system_error();
    } else {  // 创建线程成功
              //注意这里是在主线程中，先让主线程停住，因为要为子线程绑定ID、设置名字之类，而我采用的是thread_local变量来存储的，因此需要在子线程来设置
        m_sem_sync.wait();
    }
}

Thread::~Thread()
{
    // POSIX线程的一个特点是：除非线程是被分离了的，否则在线程退出时，它的资源是不会被释放的，需要调用pthread_join回收，在其所属进程退出后，才释放所有资源。
    // 而我们不希望阻塞主线程，因此这里用detach
    //  如果线程有效且不为join，将线程与主线程分离
    if (m_started && !m_joined) {
        pthread_detach(m_thread);
    }
}

Thread *Thread::GetThis() { return s_this_thread; }

pid_t Thread::GetThisId() { return s_this_tid; }

const std::string &Thread::GetThisName() { return s_this_tname; }

void Thread::SetThisName(const std::string &name) { s_this_tname = name.empty() ? DEFAULT_THREAD_NAME : name; }

void Thread::join()
{
    assert(m_started);
    assert(!m_joined);
    m_joined = true;
    int ret = pthread_join(m_thread, nullptr);
    if (ret) {
        LOG_FMT_FATAL(root_logger,
                      "pthread_join() 等待线程 %s[#%d] 失败：%s",
                      GetThisName().c_str(),
                      GetThisId(),
                      ::strerror(errno));
        throw std::system_error();
    }
}

void *Thread::Run(void *args)
{
    // 这个函数就是在子线程中
    s_this_thread = static_cast<Thread *>(args);
    try {
        // 设置线程ID
        s_this_tid = GetThreadID();
        // 设置线程名字
        s_this_tname = s_this_thread->m_tname;
        pthread_setname_np(s_this_thread->m_thread, s_this_tname.substr(0, 15).c_str());

        // REVIEW 这里针对线程函数使用swap，目的是防止线程函数中使用了智能指针导致的一些引用计数问题
        // ThreadFunc worker;
        // worker.swap(s_this_thread->m_callback);

        s_this_thread->m_sem_sync.post();  // 启动被暂停的主线程，开始并发执行
        s_this_thread->m_callback();       // 执行工作函数
    } catch (const std::exception &e) {
        LOG_FMT_FATAL(root_logger, "线程 %s[#%d] 执行异常, 原因：%s", s_this_tname.c_str(), s_this_tid, e.what());
        ::abort();
    }
    return EXIT_SUCCESS;
}

// Thread::Thread(ThreadFunc callback, const std::string &name)
//     : m_tid(-1), m_tname(name), m_thread(0), m_callback(callback), m_sem_sync(0), m_started(true), m_joined(false)
// {
//     // 调用 pthread_create 创建新线程
//     ThreadClosure *data =
//         new ThreadClosure(&m_tid, m_tname, m_callback, &m_sem_sync);  // 这个资源将在runInThread()中的unique_ptr托管
//     int result =
//         pthread_create(&m_thread, nullptr, &Thread::Run, data);  //因为Run要取地址，这就是为什么需要是static函数
//     if (result) {                                                // 创建线程失败
//         m_started = false;
//         s_this_thread = nullptr;
//         delete data;
//         LOG_FMT_FATAL(root_logger,
//                       "pthread_create() 线程创建失败, 线程名 = %s, 错误码 = %s(%d)",
//                       name.c_str(),
//                       ::strerror(result),
//                       result);
//         throw std::system_error();
//     } else {  // 创建线程成功
//         s_this_thread = this;
//         // 等待子线程启动
//         m_sem_sync.wait();
//         // m_id 储存系统线程 id, 如果小于0，说明线程启动失败
//         assert(m_tid > 0);
//     }
// }

// void *Thread::Run(void *arg)
// {
//     std::unique_ptr<ThreadClosure> data((ThreadClosure *)arg);  // 使用unique_ptr自动管理ThreadClosure指针的资源
//     data->runInThread();
//     return EXIT_SUCCESS;
// }

// Thread::ThreadClosure::ThreadClosure(pid_t *tid, const std::string &name, ThreadFunc func, Semaphore *sem)
//     : m_tid(tid), m_tname(name), m_callback(std::move(func)), m_sem_sync(sem)
// // REVIEW 这里针对线程函数使用了移动（sylar中使用的是swap），目的是防止线程函数中使用了智能指针导致的一些引用计数问题
// {}

// void Thread::ThreadClosure::runInThread()
// {
//     // 获取系统线程 id
//     *m_tid = GetThreadID();
//     m_tid = nullptr;
//     // 信号量 +1，通知主线程，子线程启动成功
//     m_sem_sync->post();
//     m_sem_sync = nullptr;
//     s_this_tid = GetThreadID();
//     s_this_tname = m_tname.empty() ? DEFAULT_THREAD_NAME : m_tname;
//     // 设置线程的名字，要求name的buffer空间不能超过16个字节，不然会报错 ERANGE
//     pthread_setname_np(pthread_self(), m_tname.substr(0, 15).c_str());
//     try {
//         m_callback();
//     } catch (const std::exception &e) {
//         LOG_FMT_FATAL(root_logger, "线程执行异常，name = %s, 原因：%s", m_tname.c_str(), e.what());
//         ::abort();
//     }
// }
}  // namespace meha