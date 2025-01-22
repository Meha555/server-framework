#include <cstring>
#include <fcntl.h>
#include <string>
extern "C" {
#include <sys/epoll.h>
#include <unistd.h>
}

#include "config.h"
#include "fiber.h"
#include "io_manager.h"
#include "module/log.h"

#include "utils/exception.h"

namespace meha
{

/* -------------------------------- FDContext ------------------------------- */

FdContext::FdContext(int fd, FdEvent ev)
    : m_fd(fd)
    , m_events(ev)
{
}

FdContext::EpollOp FdContext::addEvent(FdEvent event, Fiber::FiberFunc callback)
{
    FdContext::EpollOp op = EpollOp::Err;
    if (m_events == FdEvent::None) {
        op = EpollOp::Add;
    } else {
        op = EpollOp::Mod;
    }
    m_events = static_cast<FdEvent>(m_events | event);
    setHandler(event, Scheduler::GetCurrent(), callback);
    return op;
}

FdContext::EpollOp FdContext::delEvent(FdEvent event)
{
    FdContext::EpollOp op = EpollOp::Err;
    if (m_events == FdEvent::Read || m_events == FdEvent::Write) {
        op = EpollOp::Del;
    } else {
        op = EpollOp::Mod;
    }
    m_events = static_cast<FdEvent>(m_events & ~event);
    setHandler(event, nullptr, nullptr);
    return op;
}

void FdContext::emitEvent(FdEvent event)
{
    ASSERT(m_events & event);
    auto &handler = getHandler(event);
    if (!handler.isEmpty()) {
        ASSERT(handler.scheduler);
        if (auto fp = std::get_if<Fiber::sptr>(&handler.handle)) {
            handler.scheduler->schedule(*fp);
        } else if (auto fc = std::get_if<Fiber::FiberFunc>(&handler.handle)) {
            handler.scheduler->schedule(*fc);
        }
    }
    delEvent(event); // 清除触发状态
}

FdContext::EventHandler &FdContext::getHandler(FdEvent event)
{
    switch (event) {
    case FdEvent::Read:
        return m_readHandler;
    case FdEvent::Write:
        return m_writeHandler;
    default:
        throw std::invalid_argument("事件类型不正确");
    }
}

void FdContext::setHandler(FdEvent event, Scheduler* scheduler, const Fiber::FiberFunc &callback)
{
    if (callback) {
        getHandler(event).reset(scheduler, callback);
    } else {
        getHandler(event).reset(scheduler, Fiber::GetCurrent());
    }
}

/* -------------------------------- IOManager ------------------------------- */

#define FIND_EVENT_LISTEN(fd, event)                      \
    FdContext *fd_ctx = nullptr;                          \
    {                                                     \
        ReadScopedLock lock(&m_mutex);                    \
        if (m_fdCtxs.size() <= static_cast<size_t>(fd)) { \
            return false;                                 \
        }                                                 \
        fd_ctx = m_fdCtxs[fd].get();                      \
    }                                                     \
    ScopedLock lock(&(fd_ctx->m_mutex));                  \
    if (!(fd_ctx->m_events & (event))) {                  \
        return false;                                     \
    }

#define EVENT_LISTEN_ACTION(fd_ctx, action)                             \
    ::epoll_event epevent{};                                            \
    epevent.events = EPOLLET | fd_ctx->m_events;                        \
    epevent.data.ptr = fd_ctx;                                          \
    if (::epoll_ctl(m_epollFd, action, fd_ctx->m_fd, &epevent) == -1) { \
        return false;                                                   \
    }

// IO最大超时时间配置项（默认1s一次）
static ConfigItem<uint64_t>::sptr g_max_timeout{Config::Lookup<uint64_t>("io.max_timeout", 5000, "单位:ms")};

IOManager* IOManager::GetCurrent()
{
    return dynamic_cast<IOManager*>(Scheduler::GetCurrent());
}

IOManager::IOManager(size_t pool_size, bool use_caller)
    : Scheduler(pool_size, use_caller)
{
    // 创建 epoll fd
    m_epollFd = ::epoll_create(0xffff);
    ASSERT(m_epollFd > 0);
    // 创建管道，并加入 epoll 监听（统一事件源）
    ASSERT(::pipe(m_ticklePipe) != -1);
    // 最初需要创建监听管道的读端作为最初的监听对象
    ::epoll_event event{};
    event.data.fd = m_ticklePipe[0];
    // 1. 设置监听读就绪，边缘触发模式
    event.events = EPOLLIN | EPOLLET;
    // 2. 将管道读取端设置为非阻塞模式
    ASSERT(::fcntl(m_ticklePipe[0], F_SETFL, O_NONBLOCK) != -1);
    // 3. 将管道读端加入 epoll 监听
    ASSERT(::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_ticklePipe[0], &event) != -1);
    // 初始化 m_fdCtxs 池大小为256
    contextListResize(256);
}

IOManager::~IOManager()
{
    stop();
    // 关闭打开的文件标识符
    ::close(m_epollFd);
    ::close(m_ticklePipe[0]);
    ::close(m_ticklePipe[1]);
}

void IOManager::contextListResize(size_t size)
{
    m_fdCtxs.resize(size);
    for (size_t i = 0; i < m_fdCtxs.size(); i++) {
        if (!m_fdCtxs[i]) {
            m_fdCtxs[i] = std::make_unique<FdContext>(i);
        }
    }
}

bool IOManager::subscribeEvent(int fd, FDEvent event, Fiber::FiberFunc callback)
{
    FdContext *fd_ctx = nullptr;
    ReadScopedLock lock(&m_mutex);
    // 从 FDContext 池中拿对象
    if (m_fdCtxs.size() > static_cast<size_t>(fd)) { // 对象池足够大
        fd_ctx = m_fdCtxs[fd].get();
        lock.unLock();
    } else { // 对象池太小，扩容
        lock.unLock();
        WriteScopedLock lock2(&m_mutex);
        contextListResize(m_fdCtxs.size() << 1);
        fd_ctx = m_fdCtxs[fd].get();
    }
    ScopedLock lock3(&fd_ctx->m_mutex);
    // 如果要监听的事件已经存在，则覆盖它
    if (fd_ctx->m_events & event) {
        triggerEvent(fd, event);
    }

    EVENT_LISTEN_ACTION(fd_ctx, fd_ctx->addEvent(event, callback));
    ++m_pendingEvents;
    return true;
}

bool IOManager::unsubscribeEvent(int fd, FDEvent event)
{
    FIND_EVENT_LISTEN(fd, event);
    // 从对应的 fd_ctx 中删除该事件
    EVENT_LISTEN_ACTION(fd_ctx, fd_ctx->delEvent(event));
    // 如果该fd没有其他在监听的事件了，要从epoll对象中移除对该fd的监听
    fd_ctx->getHandler(event).reset(nullptr, std::monostate{});
    --m_pendingEvents;
    return true;
}

bool IOManager::triggerEvent(int fd, FDEvent event)
{
    FIND_EVENT_LISTEN(fd, event);
    EVENT_LISTEN_ACTION(fd_ctx, fd_ctx->delEvent(event));
    fd_ctx->emitEvent(event);
    --m_pendingEvents;
    return true;
}

bool IOManager::triggerAllEvents(int fd)
{
    FdContext *fd_ctx = nullptr;
    {
        ReadScopedLock lock(&m_mutex);
        if (m_fdCtxs.size() <= static_cast<size_t>(fd)) {
            return false;
        }
        fd_ctx = m_fdCtxs[fd].get();
    }
    ScopedLock lock(&(fd_ctx->m_mutex));
    if (!fd_ctx->m_events) {
        return true;
    }
    ::epoll_event epevent;
    epevent.events   = 0;
    epevent.data.ptr = fd_ctx;
    if (::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, &epevent) == -1) {
        return false;
    }
    if (fd_ctx->m_events & FDEvent::Read) {
        fd_ctx->emitEvent(FDEvent::Read);
        --m_pendingEvents;
    }
    if (fd_ctx->m_events & FDEvent::Write) {
        fd_ctx->emitEvent(FDEvent::Write);
        --m_pendingEvents;
    }
    fd_ctx->m_events = FDEvent::None;
    return true;
}

void IOManager::tickle()
{
    if (hasIdler()) { // tickle动作本身是对idle协程起作用的，因此必须有idle协程
        return;
    }
    if (::write(m_ticklePipe[1], "T", 1) == -1) {
        throw meha::SystemError("向子协程发送消息失败");
    }
}

bool IOManager::isStoped() const
{
    return getNextTimer() == ~0ull && m_pendingEvents == 0 && Scheduler::isStoped();
}

void IOManager::idle()
{
    // 一次epoll_wait最多检测256个就绪事件，如果就绪事件超过了这个数，那么会在下轮epoll_wati继续处理
    const uint64_t MAX_EVNETS = 256;
    auto event_list = std::make_unique<epoll_event[]>(MAX_EVNETS);

    while (true) {
        if (isStoped()) {
            break;
        }

        int result = 0;
        while (true) {
            // 默认超时时间5秒，如果下一个定时器的超时时间大于5秒，仍以5秒来计算超时，避免定时器超时时间太大时，epoll_wait一直阻塞
            uint64_t next_timeout_ms = std::min(getNextTimer(), g_max_timeout->getValue());
            // 阻塞等待 epoll 返回结果
            result = ::epoll_wait(m_epollFd, event_list.get(), MAX_EVNETS, static_cast<int>(next_timeout_ms));

            if (result < 0 && errno != EINTR) {
                LOG_FMT_WARN(core, "调度器@%p epoll_wait异常: %s(%d)", this, ::strerror(errno), errno);
            }
            if (result >= 0) {
                LOG_FMT_DEBUG(core, "epoll_wait result = %d", result);
                // 有套接字就绪，处理事件
                break;
            }
        }

        // FIXME 收集所有已超时的定时器，调度执行超时回调函数 这里的超时事件不精确吧？只有在跳出循环外才会走到并处理
        std::vector<Fiber::FiberFunc> fns;
        listExpiredCallback(fns);
        if (!fns.empty()) {
            schedule(fns.begin(), fns.end());
        }

        // 遍历 event_list 处理被触发事件的 fd
        for (int i = 0; i < result; i++) {
            ::epoll_event &ev = event_list[i];
            // 处理来自主线程的消息
            if (ev.data.fd == m_ticklePipe[0]) {
                char dummy;
                // 将来自主线程的tickle数据读取干净（读不出东西了或者读到来异常）
                while (::read(ev.data.fd, &dummy, 1) > 0);
                continue;
            }
            // 处理非主线程的消息
            auto fd_ctx = static_cast<FdContext *>(ev.data.ptr);
            ScopedLock lock(&fd_ctx->m_mutex);
            /**
             * EPOLLERR: 出错，比如写读端已经关闭的pipe
             * EPOLLHUP: 套接字对端关闭
             * 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
             */
            if (ev.events & (EPOLLERR | EPOLLHUP)) {
                ev.events |= EPOLLIN | EPOLLOUT;
            }
            auto real_events = FDEvent::None;
            if (ev.events & EPOLLIN) {
                real_events = static_cast<FDEvent>(real_events | FDEvent::Read);
            }
            if (ev.events & EPOLLOUT) {
                real_events = static_cast<FDEvent>(real_events | FDEvent::Write);
            }
            // fd_ctx 中指定监听的事件都已经被触发并处理
            if ((fd_ctx->m_events & real_events) == FDEvent::None) {
                continue;
            }
            // 下面是处理该 fd 上的就绪事件
            // 从 epoll 中移除这个 fd 的被触发的事件的监听

            // fd_ctx->delEvent(real_events);
            // EVENT_LISTEN_ACTION(fd_ctx, EPOLL_CTL_DEL);

            uint32_t left_events = (fd_ctx->m_events & ~real_events);
            int op = left_events == 0 ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
            ev.events = EPOLLET | left_events;
            int ret = ::epoll_ctl(m_epollFd, op, fd_ctx->m_fd, &ev);
            // epoll 事件修改失败，打印一条 ERROR 日志，不做任何处理
            if (ret == -1) {
                LOG_FMT_ERROR(core, "epoll_ctl(%d, %d, %d, %ul): return %d, errno %s(%d)", m_epollFd, op, fd_ctx->m_fd, ev.events, ret, ::strerror(errno), errno);
            }
            // 触发该 fd 对应的事件的处理器
            if (real_events & FDEvent::Read) {
                fd_ctx->emitEvent(FDEvent::Read);
                --m_pendingEvents;
            }
            if (real_events & FDEvent::Write) {
                fd_ctx->emitEvent(FDEvent::Write);
                --m_pendingEvents;
            }
        }
        // 让出当前线程的执行权，给调度器执行其他排队等待的协程（IO协程调度器的好处）
        Fiber::sptr current_fiber = Fiber::GetCurrent();
        auto raw_ptr = current_fiber.get();
        current_fiber.reset(); // REVIEW 这里真的有必要这样让fiber的生命周期在执行下一个函数前失效吗
        raw_ptr->yield();
    }
}

void IOManager::onTimerInsertedAtFront()
{
    tickle();
}

#undef FIND_EVENT_LISTEN
#undef EVENT_LISTEN_ACTION

} // namespace meha
