#include "io_manager.h"
#include "exception.h"
#include "fiber.h"
#include "log.h"
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <variant>

namespace meha
{

static Logger::sptr root_logger = GET_LOGGER("root");

/* -------------------------------- FDContext ------------------------------- */

void FdContext::addEvent(FdEvent event)
{
    m_events = static_cast<FdEvent>(m_events | event);
}

void FdContext::delEvent(FdEvent event)
{
    m_events = static_cast<FdEvent>(m_events & ~event);
}

void FdContext::triggerEvent(FdEvent event)
{
    ASSERT(m_events & event);
    delEvent(event); // 清除触发状态
    auto &handler = getHandler(event);
    if (!handler.isEmpty()) {
        if (auto fp = std::get_if<Fiber::sptr>(&handler.handle)) {
            handler.scheduler->schedule(std::move(*fp));
        } else if (auto fc = std::get_if<Fiber::FiberFunc>(&handler.handle)) {
            handler.scheduler->schedule(std::move(*fc));
        }
    }
    ClearHandler(handler); // 从FDContext中删除该事件对应的信息
}

FdContext::EventHandler &FdContext::getHandler(FdEvent event)
{
    switch (event) {
    case FdEvent::READ:
        return m_read_handler;
    case FdEvent::WRITE:
        return m_write_handler;
    default:
        throw std::invalid_argument("事件类型不正确");
    }
}

void FdContext::setHandler(FdEvent event, Scheduler::sptr scheduler, const Fiber::FiberFunc &callback)
{
    auto &ev_handler = getHandler(event);
    ev_handler.reset(scheduler, callback);
}

void FdContext::ClearHandler(FdContext::EventHandler &handler)
{
    handler.reset(nullptr, nullptr);
}

/* -------------------------------- IOManager ------------------------------- */

#define FIND_EVENT_LISTEN(fd, event)                      \
    FdContext *fd_ctx = nullptr;                          \
    {                                                     \
        ReadScopedLock lock(&m_mutex);                    \
        if (m_fdctxs.size() <= static_cast<size_t>(fd)) { \
            return false;                                 \
        }                                                 \
        fd_ctx = m_fdctxs[fd].get();                      \
    }                                                     \
    ScopedLock lock(&(fd_ctx->m_mutex));                  \
    if (!(fd_ctx->m_events & event)) {                    \
        return false;                                     \
    }

#define EVENT_LISTEN_ACTION(fd_ctx, epoll_action)                                    \
    const int op = fd_ctx->m_events == FDEvent::NONE ? epoll_action : EPOLL_CTL_MOD; \
    ::epoll_event epevent{};                                                         \
    epevent.events = EPOLLET | fd_ctx->m_events;                                     \
    epevent.data.ptr = fd_ctx;                                                       \
    if (::epoll_ctl(m_epoll_fd, op, fd_ctx->m_fd, &epevent) == -1) {                 \
        return false;                                                                \
    }

IOManager::sptr IOManager::GetCurrent()
{
    return std::dynamic_pointer_cast<IOManager>(Scheduler::GetCurrent());
}

IOManager::IOManager(size_t pool_size, bool use_caller)
    : Scheduler(pool_size, use_caller)
{
    // 创建 epoll fd
    m_epoll_fd = ::epoll_create(0xffff);
    ASSERT(m_epoll_fd > 0);
    // 创建管道，并加入 epoll 监听（统一事件源）
    ASSERT(::pipe(m_tickle_fds) != -1);
    // 最初需要创建监听管道的读端作为最初的监听对象
    ::epoll_event event{};
    event.data.fd = m_tickle_fds[0];
    // 1. 设置监听读就绪，边缘触发模式
    event.events = EPOLLIN | EPOLLET;
    // 2. 将管道读取端设置为非阻塞模式
    ASSERT(::fcntl(m_tickle_fds[0], F_SETFL, O_NONBLOCK) != -1);
    // 3. 将管道读端加入 epoll 监听
    ASSERT(::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_tickle_fds[0], &event) != -1);
    contextListResize(64);
    // 启动调度器
    start();
}

IOManager::~IOManager()
{
    stop();
    // 关闭打开的文件标识符
    ::close(m_epoll_fd);
    ::close(m_tickle_fds[0]);
    ::close(m_tickle_fds[1]);
}

void IOManager::contextListResize(size_t size)
{
    m_fdctxs.resize(size); // REVIEW 这里用reserve怎么样？？
    for (size_t i = 0; i < m_fdctxs.size(); i++) {
        if (!m_fdctxs[i]) {
            m_fdctxs[i] = std::make_unique<FdContext>();
            m_fdctxs[i]->m_fd = i;
        }
    }
}

bool IOManager::subscribeEvent(int fd, FDEvent event, Fiber::FiberFunc callback)
{
    FdContext *fd_ctx = nullptr;
    ReadScopedLock lock(&m_mutex);
    // 从 FDContext 池中拿对象
    if (m_fdctxs.size() > static_cast<size_t>(fd)) { // 对象池足够大
        fd_ctx = m_fdctxs[fd].get();
        lock.unlock();
    } else { // 对象池太小，扩容
        lock.unlock();
        WriteScopedLock lock2(&m_mutex);
        contextListResize(m_fdctxs.size() << 1);
        fd_ctx = m_fdctxs[fd].get();
    }
    ScopedLock lock3(&fd_ctx->m_mutex);
    // 如果要监听的事件已经存在，则覆盖它
    if (fd_ctx->m_events & event) {
        triggerEvent(fd, event);
    }

    fd_ctx->addEvent(event);
    EVENT_LISTEN_ACTION(fd_ctx, EPOLL_CTL_ADD);
    ++m_pending_event_count;
    return true;
}

bool IOManager::unsubscribeEvent(int fd, FDEvent event)
{
    FIND_EVENT_LISTEN(fd, event);
    // 从对应的 fd_ctx 中删除该事件
    fd_ctx->delEvent(event);
    EVENT_LISTEN_ACTION(fd_ctx, EPOLL_CTL_DEL);
    // 如果该fd没有其他在监听的事件了，要从epoll对象中移除对该fd的监听
    FdContext::ClearHandler(fd_ctx->getHandler(event));
    --m_pending_event_count;
    return true;
}

bool IOManager::triggerEvent(int fd, FDEvent event)
{
    FIND_EVENT_LISTEN(fd, event);
    fd_ctx->delEvent(event);
    EVENT_LISTEN_ACTION(fd_ctx, EPOLL_CTL_DEL);
    fd_ctx->triggerEvent(event);
    --m_pending_event_count;
    return true;
}

bool IOManager::triggerAllEvents(int fd)
{
    FIND_EVENT_LISTEN(fd, 1);
    if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        return false;
    }
    if (fd_ctx->m_events & FDEvent::READ) {
        fd_ctx->triggerEvent(FDEvent::READ);
        --m_pending_event_count;
    }
    if (fd_ctx->m_events & FDEvent::WRITE) {
        fd_ctx->triggerEvent(FDEvent::WRITE);
        --m_pending_event_count;
    }
    fd_ctx->m_events = FDEvent::NONE;
    return true;
}

void IOManager::tickle()
{
    if (hasIdleThread()) { // tickle动作本身是对idle协程起作用的，因此必须有idle协程
        return;
    }
    if (::write(m_tickle_fds[1], "T", 1) == -1) {
        throw meha::SystemError("向子线程发送消息失败");
    }
}

bool IOManager::isStoped() const
{
    return getNextTimer() == ~0ull && m_pending_event_count == 0 && Scheduler::isStoped();
}

void IOManager::idle()
{
    auto event_list = std::make_unique<epoll_event[]>(64);

    while (true) {
        if (isStoped()) {
            LOG_FMT_DEBUG(root_logger, "调度器@%p 已停止执行", this);
            break;
        }

        int result = 0;
        while (true) {
            static constexpr uint64_t MAX_TIMEOUT = 1000;
            uint64_t next_timeout = std::min(getNextTimer(), MAX_TIMEOUT);
            // 阻塞等待 epoll 返回结果
            result = ::epoll_wait(m_epoll_fd, event_list.get(), 64,
                                  static_cast<int>(next_timeout));

            if (result < 0 && errno != EINTR) {
                LOG_FMT_WARN(root_logger, "调度器@%p epoll_wait异常: %s(%d)", this, ::strerror(errno), errno);
            }
            if (result >= 0) {
                // 有套接字就绪，处理事件
                break;
            }
        }

        // 记得处理一下定时事件 // FIXME 这里的超时事件不精确吧？只有在跳出循环外才会走到并处理
        std::vector<Fiber::FiberFunc> fns;
        listExpiredCallback(fns);
        if (!fns.empty()) {
            schedule(fns.begin(), fns.end());
        }

        // 遍历 event_list 处理被触发事件的 fd
        for (int i = 0; i < result; i++) {
            ::epoll_event &ev = event_list[i];
            // 处理来自主线程的消息
            if (ev.data.fd == m_tickle_fds[0]) {
                char dummy;
                // 将来自主线程的数据读取干净
                while (true) {
                    int status = ::read(ev.data.fd, &dummy, 1); // TODO 这里是不是可以用缓冲读取（注意这里read会更改seek指针？但是这里fd是套接字诶）
                    if (status == 0 || status == -1) // 尽可能读完（读不出东西了或者读到来异常）
                        break;
                }
                continue;
            }
            // 处理非主线程的消息
            auto fd_ctx = static_cast<FdContext *>(ev.data.ptr);
            ScopedLock lock(&fd_ctx->m_mutex);
            // 该事件的 fd 出现错误或者已经失效
            if (ev.events & (EPOLLERR | EPOLLHUP)) {
                ev.events |= EPOLLIN | EPOLLOUT; // 我们标记这种情况为可读可写
            }
            auto real_events = FDEvent::NONE;
            if (ev.events & EPOLLIN) {
                real_events = static_cast<FDEvent>(real_events | FDEvent::READ);
            }
            if (ev.events & EPOLLOUT) {
                real_events = static_cast<FDEvent>(real_events | FDEvent::WRITE);
            }
            // fd_ctx 中指定监听的事件都已经被触发并处理
            if ((fd_ctx->m_events & real_events) == FDEvent::NONE) {
                continue;
            }
            // 下面是处理该 fd 上的就绪事件
            // 从 epoll 中移除这个 fd 的被触发的事件的监听

            // fd_ctx->delEvent(real_events);
            // EVENT_LISTEN_ACTION(fd_ctx, EPOLL_CTL_DEL);

            uint32_t left_events = (fd_ctx->m_events & ~real_events);
            int op = left_events == 0 ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
            ev.events = EPOLLET | left_events;
            int ret = ::epoll_ctl(m_epoll_fd, op, fd_ctx->m_fd, &ev);
            // epoll 事件修改失败，打印一条 ERROR 日志，不做任何处理
            if (ret == -1) {
                LOG_FMT_ERROR(root_logger, "epoll_ctl(%d, %d, %d, %ul): return %d, errno %s(%d)", m_epoll_fd, op, fd_ctx->m_fd, ev.events, ret, ::strerror(errno), errno);
            }
            // 触发该 fd 对应的事件的处理器
            if (real_events & FDEvent::READ) {
                fd_ctx->triggerEvent(FDEvent::READ);
                --m_pending_event_count;
            }
            if (real_events & FDEvent::WRITE) {
                fd_ctx->triggerEvent(FDEvent::WRITE);
                --m_pending_event_count;
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
