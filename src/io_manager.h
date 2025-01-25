#pragma once

#include <atomic>
#include <memory>
#include <sys/epoll.h>
#include <variant>

#include "scheduler.h"
#include "timer.h"

namespace meha
{

/**
 * @brief fd就绪事件上下文
 * @details <fd, event, callback>
 */
class FdContext
{
    friend class IOManager;

public:
    enum FdEvent { None = 0x0,
                   Read = 0x1,
                   Write = 0x4 };
    enum EpollOp {
        Err = 0,
        Add = EPOLL_CTL_ADD,
        Mod = EPOLL_CTL_MOD,
        Del = EPOLL_CTL_DEL,
    };
    struct EventHandler
    {
        Scheduler *scheduler = nullptr; // 指定处理该事件的调度器
        std::variant<std::monostate, Fiber::sptr, Fiber::FiberFunc> handle = std::monostate{}; // 处理该事件的回调句柄
        bool isEmpty() const
        {
            return std::holds_alternative<std::monostate>(handle);
        }
        // 重设当前EventHandler
        void reset(Scheduler *sche, const decltype(handle) &cb)
        {
            scheduler = sche;
            handle = cb;
        }
    };

public:
    explicit FdContext(int fd, FdEvent ev = FdEvent::None);

    // 添加监听指定的事件
    EpollOp addEvent(FdEvent event, Fiber::FiberFunc callback);
    // 取消监听指定的事件
    EpollOp delEvent(FdEvent event);
    // 触发事件，然后删除该事件相关的信息
    void emitEvent(FdEvent event);

    // 获取指定事件的处理器
    EventHandler &getHandler(FdEvent event);
    // 设置指定事件的处理器
    void setHandler(FdEvent event, Scheduler *scheduler, const Fiber::FiberFunc &callback);

private:
    mutable Mutex m_mutex;
    int m_fd; // 要监听的文件描述符
    FdEvent m_events = FdEvent::None; // 要监听的事件掩码集
    EventHandler m_readHandler; // 读就绪事件处理器
    EventHandler m_writeHandler; // 写就绪事件处理器
};

/**
 * @brief IO协程调度
 * @details 用于监听套接字
 */
class IOManager final : public Scheduler, public TimerManager
{
public:
    using sptr = std::shared_ptr<IOManager>;
    using FDEvent = FdContext::FdEvent;

public:
    explicit IOManager(size_t pool_size, bool use_caller = true);
    ~IOManager() override;

    // thread-safe 给指定的 fd 增加事件监听，当 callback 是 nullptr 时，将当前上下文转换为协程，并作为事件回调使用
    bool subscribeEvent(int fd, FDEvent event, Fiber::FiberFunc callback = nullptr);
    // thread-safe 给指定的 fd 移除指定的事件监听
    bool unsubscribeEvent(int fd, FDEvent event);
    // thread-safe 立即触发指定 fd 的指定的事件回调，然后移除该事件
    bool triggerEvent(int fd, FDEvent event);
    // thread-safe 立即触发指定 fd 的所有事件回调，然后移除所有的事件
    bool triggerAllEvents(int fd);

    static IOManager *GetCurrent();

protected:
    void tickle() override;
    void idle() override;
    bool isStoped() const override;
    void contextListResize(size_t size);

    void onTimerInsertedAtFront() override;

private:
    int m_epollFd = 0;
    int m_ticklePipe[2]{0, 0}; // 主协程给子协程发消息用的管道（0读1写）
    std::atomic_size_t m_pendingEvents{0}; // 等待执行的IO事件的数量
    std::vector<std::unique_ptr<FdContext>> m_fdCtxs{}; // FDContext 的对象池，下标对应 fd id。这个用map会不会更好，为啥需要像select那样准备一大串fd呢？或者说这里有必要池化吗？
    mutable RWMutex m_mutex;
};
} // namespace meha
