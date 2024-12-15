#pragma once

#include "scheduler.h"
#include "thread.h"
#include "timer.h"
#include <atomic>
#include <functional>
#include <memory>
#include <variant>

namespace meha
{

/**
 * @brief fd就绪事件上下文
 * @details <fd, event, callback>
 */
class FDContext
{
    friend class IOManager;

public:
    enum FDEvent { NONE = 0x0,
                   READ = 0x1,
                   WRITE = 0x4 };
    struct EventHandler
    {
        Scheduler::sptr scheduler = nullptr; // 指定处理该事件的调度器
        std::variant<std::monostate, Fiber::sptr, Fiber::FiberFunc> handle = std::monostate{}; // 处理该事件的回调句柄
        bool isEmpty() const
        {
            return std::holds_alternative<std::monostate>(handle);
        }
        // 重设当前EventHandler
        void reset(Scheduler::sptr sche, const Fiber::FiberFunc &cb)
        {
            if (scheduler || isEmpty()) {
                reset(nullptr, nullptr);
            }
            if (cb) {
                scheduler = sche;
                handle = std::move(cb);
            } else {
                scheduler = nullptr;
                handle = std::monostate{};
            }
        }
    };

public:
    // 添加监听指定的事件
    void addEvent(FDEvent event);
    // 取消监听指定的事件
    void delEvent(FDEvent event);
    // 触发事件，然后删除该事件相关的信息
    void triggerEvent(FDEvent event);

    // 获取指定事件的处理器
    EventHandler &getHandler(FDEvent event);
    // 设置指定事件的处理器
    void setHandler(FDEvent event, Scheduler::sptr scheduler, const Fiber::FiberFunc &callback);
    // 清除指定的事件处理器
    static inline void ClearHandler(EventHandler &handler);

private:
    mutable Mutex m_mutex;
    int m_fd; // 要监听的文件描述符
    FDEvent m_events = FDEvent::NONE; // 要监听的事件掩码集
    EventHandler m_read_handler; // 读就绪事件处理器
    EventHandler m_write_handler; // 写就绪事件处理器
};

/**
 * @brief IO协程调度
 * @details 用于监听套接字
 */
class IOManager final : public Scheduler, public TimerManager
{
public:
    using sptr = std::shared_ptr<IOManager>;
    using FDEvent = FDContext::FDEvent;

public:
    explicit IOManager(size_t pool_size, bool use_caller = false);
    ~IOManager() override;

    // thread-safe 给指定的 fd 增加事件监听，当 callback 是 nullptr
    // 时，将当前上下文转换为协程，并作为事件回调使用
    bool addEventListen(int fd, FDEvent event, Fiber::FiberFunc callback = nullptr);
    // thread-safe 给指定的 fd 移除指定的事件监听
    bool removeEventListen(int fd, FDEvent event);
    // thread-safe 立即触发指定 fd 的指定的事件回调，然后移除该事件
    bool cancelEventListen(int fd, FDEvent event);
    // thread-safe 立即触发指定 fd 的所有事件回调，然后移除所有的事件
    bool cancelAllEventListen(int fd);

public:
    static IOManager::sptr GetCurrent();

protected:
    void tickle() override;
    void idle() override;
    bool isStoped() const override;
    void contextListResize(size_t size);

    void onTimerInsertedAtFront() override;

private:
    int m_epoll_fd = 0;
    int m_tickle_fds[2]{0, 0}; // 主线程给子线程发消息用的管道（0读1写）
    std::atomic_size_t m_pending_event_count{0}; // 等待执行的IO事件的数量
    mutable RWLock m_mutex;
    std::vector<std::unique_ptr<FDContext>> m_fdctxs{}; // FDContext 的对象池，下标对应 fd id。这个用map会不会更好，为啥需要像select那样准备一大串fd呢？或者说这里有必要池化吗？
};
} // namespace meha
