#ifndef SERVER_FRAMEWORK_THREAD_H
#define SERVER_FRAMEWORK_THREAD_H

#include "mutex.hpp"
#include <functional>
#include <memory>

namespace meha {

/**
 * @brief 线程类
 * @note 执行的线程函数如果含有参数，需要使用std::bind先构造偏函数
 */
class Thread : public noncopyable {
public:
    using sptr = std::shared_ptr<Thread>;
    using uptr = std::unique_ptr<Thread>;
    using ThreadFunc = std::function<void()>;

    // /**
    //  * @brief 线程数据类
    //  * @details 封装线程执行需要的数据，作为UserData传入线程
    //  */
    // struct ThreadClosure
    // {
    //     pid_t *m_tid;
    //     std::string m_tname;
    //     ThreadFunc m_callback;
    //     Semaphore *m_sem_sync;
    //     ThreadClosure(pid_t *tid, const std::string &name, ThreadFunc func, Semaphore *sem);
    //     // 根据传入的UserData启动线程
    //     void runInThread();
    // };

    Thread(ThreadFunc callback, const std::string &name);
    ~Thread();
    // 获取线程 id
    // pid_t getId() const { return m_tid; }
    // 获取线程名称
    // const std::string &getName() const { return m_tname; }
    // 设置线程名称
    // void setName(const std::string &name) { m_tname = name; }
    // 将线程并入主线程
    void join();

    // 获取当前线程指针
    static Thread *GetThis();
    // 获取当前线程的系统线程 id
    static pid_t GetThisId();
    // 获取当前运行线程的名称
    static const std::string &GetThisName();
    // 设置当前运行线程的名称（这个静态的setter是为了能够给主线程命名，因为主线程不是我们创建的，所以没有对应的Thread对象，而我们又想要对其进行操作，所以利用了thread_local）
    static void SetThisName(const std::string &name);
    // 提供一个启动线程的通用POSIX接口给Thread类, 接收POSIX参数，内部完成启动线程的真正逻辑
    static void *Run(void *arg);

private:
    // 系统线程tid, 通过 syscall() 获取
    pid_t m_tid;
    // 线程名称
    std::string m_tname;
    // pthread 线程句柄(id)
    pthread_t m_thread;
    // 线程执行的函数
    ThreadFunc m_callback;
    // 控制线程启动的信号量（用于保证线程创建成功之后再执行线程函数）
    Semaphore m_sem_sync;
    // 线程状态
    bool m_started;
    bool m_joined;
};
}  // namespace meha

#endif
