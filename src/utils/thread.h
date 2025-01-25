#pragma once

#include <functional>

#include "macro.h"
#include "utils/sem.h"

namespace meha
{

/**
 * @brief 线程管理类（不可拷贝但可移动），类似于QThread，线程本身是pthread
 * @note 执行的线程函数如果含有参数，需要使用std::bind先构造偏函数
 */
class Thread : public std::enable_shared_from_this<Thread>
{
    friend struct ThreadClosure;
    DISABLE_COPY(Thread)
public:
    using sptr = std::shared_ptr<Thread>;
    using ThreadFunc = std::function<void()>;

    enum Status {
        Ready,
        Running,
        Stoped,
        Joined,
        Detached,
        Error,
    };

    /**
     * @brief 线程数据类（进一步分离Thread类所作的工作）
     * @details 封装线程执行需要的数据，作为UserData传入线程
     * @note 这里用这个内部类属于是将简单问题复杂化，这里仅本着学习目的作为一种思路的体现
     */
    struct ThreadClosure
    {
        ThreadClosure(const ThreadFunc &callback, void *user_data = nullptr)
            : callback(std::move(callback)) // FIXME const修饰的callback还能move吗
            , user_data(user_data)
        {
        }
        ThreadFunc callback;
        void *user_data;
        void runInThread(); // 根据传入的UserData启动线程，类似于QObject::moveToThread
    };

    explicit Thread(ThreadFunc callback);
    ~Thread();

    void start();
    void stop();
    void join();
    void detach();

    Status status() const;
    pid_t tid() const;

    // 提供一个启动线程的通用POSIX接口给Thread类, 接收POSIX参数，内部完成启动线程的真正逻辑
    static void *Run(void *arg);

    // 获取当前线程指针
    static Thread::sptr GetCurrent(); // FIXME 这里应该返回什么指针更好？

private:
    // linux线程id
    pid_t m_tid;
    // pthread 线程句柄(id)
    pthread_t m_thread;
    // 线程执行的函数
    ThreadFunc m_callback;
    // 控制线程启动的信号量（用于保证线程创建成功之后再执行线程函数）
    Semaphore m_sem;
    // 线程状态
    Status m_status;
};
} // namespace meha
