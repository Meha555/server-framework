// #ifndef SERVER_FRAMEWORK_THREAD_H
// #define SERVER_FRAMEWORK_THREAD_H
#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "mutex.hpp"
#include "sem.h"

namespace meha {

/**
 * @brief 线程管理类（不可拷贝但可移动），类似于QThread，线程本身是pthread
 * @note 执行的线程函数如果含有参数，需要使用std::bind先构造偏函数
 */
class Thread : public noncopyable {
public:
    using sptr = std::shared_ptr<Thread>;
    using uptr = std::unique_ptr<Thread>;
    using ThreadFunc = std::function<void()>;

    /**
     * @brief 线程数据类（进一步分离Thread类所作的工作）
     * @details 封装线程执行需要的数据，作为UserData传入线程
     * @note 这里用这个内部类属于是将简单问题复杂化，这里仅本着学习目的作为一种思路的体现
     */
    struct ThreadClosure
    {
        ThreadClosure(const std::string_view &name, const ThreadFunc &callback, void *user_data = nullptr)
            : name(name),
              callback(std::move(callback)),
              user_data(user_data)
        {}
        std::string_view name;
        ThreadFunc callback;
        void *user_data;
        void runInThread();  // 根据传入的UserData启动线程，类似于moveToThread
    };

    explicit Thread(ThreadFunc callback, const std::string_view &name = "annoymous_thread");
    ~Thread();

    //  将线程并入主线程
    void join();
    // 提供一个启动线程的通用POSIX接口给Thread类, 接收POSIX参数，内部完成启动线程的真正逻辑
    static void *Run(void *arg);

    // 获取当前线程指针
    static Thread *GetCurrent();
    // 获取当前线程的系统线程 id
    static pid_t GetCurrentId();
    // 获取当前运行线程的名称
    static const std::string_view &GetCurrentName();
    // 设置当前运行线程的名称（这个静态的setter是为了能够给主线程命名，因为主线程不是我们创建的，所以没有对应的Thread对象，而我们又想要对其进行操作，所以利用了thread_local）
    static void SetCurrentName(const std::string_view &name = "annoymous_thread");

private:
    // pthread 线程句柄(id)
    pthread_t m_thread;
    // 线程执行的函数
    ThreadFunc m_callback;
    // 控制线程启动的信号量（用于保证线程创建成功之后再执行线程函数）
    ThreadSemaphore m_sem_sync;
    // 线程状态
    bool m_started;
    bool m_joined;
};
}  // namespace meha

// #endif
