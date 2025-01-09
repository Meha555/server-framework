#include <fmt/format.h>

#include "module/log.h"

#include "thread.h"
#include "utils/exception.h"
#include "utils/sem.h"

// FIXME - 这些底层库的失败和异常的处理，应该怎么做？是不是可以参考一下Java？参考下boost库？

namespace meha
{

// 当前线程的 Thread 实例的指针，如果不是我们用Thread创建线程，这个指针变量就是初值nullptr
static thread_local Thread::sptr t_this_thread{nullptr}; // 这个指针用什么智能指针？weakptr支持不用的时候不影响引用计数，用的时候再取出shared_ptr随用随放，所以粒度控制的很小

void Thread::ThreadClosure::runInThread()
{
    t_this_thread = static_cast<Thread *>(user_data)->shared_from_this();

    // REVIEW 这里针对线程函数使用swap，目的是防止线程函数ThreadFunc中使用了智能指针导致的一些引用计数问题
    ThreadFunc worker;
    worker.swap(t_this_thread->m_callback);

    try {
        t_this_thread->m_status = Status::Running;
        t_this_thread->m_tid = utils::GetThreadID();
        t_this_thread->m_sem.post(); // 启动被暂停的主线程，开始并发执行
        worker(); // 执行工作函数
    } catch (const std::exception &e) {
        throw RuntimeError(fmt::format("线程[TID:{}] 执行异常, 原因：{}", utils::GetThreadID(), e.what()));
    }
}

Thread::Thread(ThreadFunc callback)
    : m_thread(0)
    , m_callback(std::move(callback))
    , m_sem(0)
    , m_status(Status::Ready)
{
}

Thread::~Thread()
{
    // 而我们不希望阻塞主线程，因此这里用detach
    // 如果线程有效且不为join，将线程与主线程分离，此后线程的资源使用和清理由线程函数Thread::Run自己保证
    detach();
}

void Thread::start()
{
    if (m_status != Status::Ready) {
        return;
    }
    ThreadClosure *closure = new ThreadClosure(m_callback, this);
    int ret = pthread_create(&m_thread, nullptr, &Thread::Run, closure);
    if (ret) { // 创建线程失败
        t_this_thread.reset();
        delete closure;
        m_status = Status::Error;
    } else { // 创建线程成功
        // 注意这里是在主线程中，先让主线程停住，因为要为子线程绑定ID、设置名字之类，且还没那么快开跑任务worker，所以需要同步一下
        // 我采用的是thread_local变量来存储的上述信息的，因此需要在子线程来设置，主线程等待
        m_sem.wait();
        LOG_FMT_TRACE(core, "start 线程[TID:%d]", tid());
    }
}

void Thread::stop()
{
    if (m_status == Status::Stoped) {
        return;
    }
    m_status = Status::Stoped;
    pthread_cancel(m_thread);
    LOG_FMT_TRACE(core, "cancel 线程[TID:%d]", tid());
}

void Thread::join()
{
    if (m_status != Status::Running) {
        return;
    }
    // 线程只能join一次，且只能在Running的状态被join
    int ret = pthread_join(m_thread, nullptr);
    if (ret) {
        throw SystemError(fmt::format("pthread_join() 等待线程[TID:{}] 失败", tid()));
    }
    m_status = Status::Joined;
    LOG_FMT_TRACE(core, "join 线程[TID:%d]", tid());
}

void Thread::detach()
{
    if (m_status != Status::Running) {
        return;
    }
    m_status = Status::Detached;
    pthread_detach(m_thread);
    LOG_FMT_TRACE(core, "detach 线程[TID:%d]", tid());
}

Thread::Status Thread::status() const
{
    return m_status;
}

pid_t Thread::tid() const
{
    ASSERT(m_status != Status::Ready && m_status != Status::Error);
    return m_tid;
}

void *Thread::Run(void *args)
{
    // 这个函数就是在子线程中
    auto closure = static_cast<ThreadClosure *>(args);
    closure->runInThread();
    delete closure;
    return EXIT_SUCCESS;
}

Thread::sptr Thread::GetCurrent()
{
    return t_this_thread;
}

} // namespace meha