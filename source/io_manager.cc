#include "io_manager.h"
#include "exception.h"
#include "fiber.h"
#include "log.h"
#include <array>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <variant>

namespace meha {

static Logger::ptr root_logger = GET_LOGGER("root");

/* -------------------------------- IOManager ------------------------------- */

#define FIND_EVENT_LISTEN(fd, event)                                           \
  FDContext *fd_ctx = nullptr;                                                 \
  {                                                                            \
    ReadScopedLock lock(&m_lock);                                              \
    if (m_fdctx_list.size() <= static_cast<size_t>(fd)) {                      \
      return false;                                                            \
    }                                                                          \
    fd_ctx = m_fdctx_list[fd].get();                                           \
  }                                                                            \
  ScopedLock lock(&(fd_ctx->mutex));                                           \
  if (!(fd_ctx->events & event)) {                                             \
    return false;                                                              \
  }

#define DEL_EVENT_LISTEN(fd, event)                                            \
  FIND_EVENT_LISTEN(fd, event);                                                \
  fd_ctx->cancelEvent(event);                                                  \
  const int op =                                                               \
      fd_ctx->events == FDEvent::NONE ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;         \
  epoll_event epevent{};                                                       \
  epevent.events = EPOLLET | fd_ctx->events;                                   \
  epevent.data.ptr = fd_ctx;                                                   \
  ::epoll_ctl(m_epoll_fd, op, fd, &epevent);

IOManager *IOManager::GetCurrent() {
  return dynamic_cast<IOManager *>(Scheduler::GetCurrent());
}

IOManager::IOManager(size_t pool_size, bool use_caller)
    : Scheduler(pool_size, use_caller) {
  // 创建 epoll fd
  m_epoll_fd = ::epoll_create(0xffff);
  ASSERT(m_epoll_fd > 0);
  // 创建管道，并加入 epoll 监听
  ASSERT(::pipe(m_tickle_fds) > 0);
  // 最初需要创建监听管道的读端作为最初的监听对象
  epoll_event event{};
  event.data.fd = m_tickle_fds[0];
  // 1. 设置监听读就绪，边缘触发模式
  event.events = EPOLLIN | EPOLLET;
  // 2. 将管道读取端设置为非阻塞模式
  ASSERT(::fcntl(m_tickle_fds[0], F_SETFL, O_NONBLOCK) > 0);
  // 3. 将管道读端加入 epoll 监听
  ASSERT(::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_tickle_fds[0], &event) > 0);
  contextListResize(64);
  // 启动调度器
  start();
}

IOManager::~IOManager() {
  stop();
  // 关闭打开的文件标识符
  close(m_epoll_fd);
  close(m_tickle_fds[0]);
  close(m_tickle_fds[1]);
  // 释放 m_fd_context_list 的指针
  //    for (auto item : m_fd_context_list)
  //    {
  //        delete item;
  //    }
}

void IOManager::contextListResize(size_t size) {
  m_fdctx_list.resize(size);
  for (size_t i = 0; i < m_fdctx_list.size(); i++) {
    if (!m_fdctx_list[i]) {
      m_fdctx_list[i] = std::make_unique<FDContext>();
      m_fdctx_list[i]->fd = i;
    }
  }
}

bool IOManager::addEventListen(int fd, FDEvent event,
                               Fiber::FiberFunc callback) {
  FDContext *fd_ctx = nullptr;
  ReadScopedLock lock(&m_lock);
  // 从 m_fd_context_list 中拿对象
  if (m_fdctx_list.size() > static_cast<size_t>(fd)) { // 对象池足够大
    fd_ctx = m_fdctx_list[fd].get();
    lock.unlock();
  } else { // 对象池太小，扩容
    lock.unlock();
    WriteScopedLock lock2(&m_lock);
    contextListResize(m_fdctx_list.size() << 1);
    fd_ctx = m_fdctx_list[fd].get();
  }
  ScopedLock lock3(&fd_ctx->mutex);
  // 如果要监听的事件已经存在，则覆盖它
  if (fd_ctx->events & event) {
    cancelEventListen(fd, event);
  }

  int op = fd_ctx->events == FDEvent::NONE ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
  epoll_event epevent{};
  epevent.events = EPOLLET | fd_ctx->events | event;
  /**
   * FIXME: 感觉这是个不太好的做法。 fd_ctx 指向的对象由 unique_ptr 管理，
   *        这相当于交出了所有权，但暂时想不出解决办法。
   * */
  epevent.data.ptr = fd_ctx;
  if (::epoll_ctl(m_epoll_fd, op, fd, &epevent) == -1) {
    return false;
  }
  ++m_pending_event_count;
  fd_ctx->addEvent(event);
  FDContext::EventHandler &ev_handler = fd_ctx->getHandler(event);

  return 0;
}

bool IOManager::removeEventListen(int fd, FDEvent event) {
  DEL_EVENT_LISTEN(fd, event);
  // 如果该fd没有其他在监听的事件了，要从epoll对象中移除对该fd的监听
  fd_ctx->clearHandler(fd_ctx->getHandler(event));
  --m_pending_event_count;
  return true;
}

bool IOManager::cancelEventListen(int fd, FDEvent event) {
  DEL_EVENT_LISTEN(fd, event);
  fd_ctx->triggerEvent(event);
  --m_pending_event_count;
  return true;
}

bool IOManager::cancelAll(int fd) {
  FIND_EVENT_LISTEN(fd, 1);
  ASSERT(::epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr) > 0);
  if (fd_ctx->events & FDEvent::READ) {
    fd_ctx->triggerEvent(FDEvent::READ);
    --m_pending_event_count;
  }
  if (fd_ctx->events & FDEvent::WRITE) {
    fd_ctx->triggerEvent(FDEvent::WRITE);
    --m_pending_event_count;
  }
  fd_ctx->events = FDEvent::NONE;
  return true;
}

void IOManager::tickle() {
  if (hasIdleThread()) { // REVIEW 为什么要这个
    return;
  }
  if (write(m_tickle_fds[1], "T", 1) == -1) {
    throw meha::SystemError("向子线程发送消息失败");
  }
}

bool IOManager::isStoped() const {
  uint64_t timeout;
  return isStoped(timeout);
}

bool IOManager::isStoped(uint64_t &timeout) const {
  timeout = getNextTimer();
  return timeout == ~0ull && m_pending_event_count == 0 &&
         Scheduler::isStoped();
}

void IOManager::doIdle() {
  auto event_list = std::make_unique<epoll_event[]>(64);

  while (true) {
    uint64_t next_timeout = 0;
    if (isStoped(next_timeout)) {
      // 没有等待执行的定时器
      if (next_timeout == ~0ull) {
        LOG_FMT_DEBUG(root_logger, "调度器@%p 已停止执行", this);
        break;
      }
    }

    int result = 0;
    while (true) {
      static const int MAX_TIMEOUT = 1000;
      if (next_timeout != ~0ull) {
        next_timeout = static_cast<int>(next_timeout) > MAX_TIMEOUT
                           ? MAX_TIMEOUT
                           : next_timeout;
      } else {
        next_timeout = MAX_TIMEOUT;
      }
      // 阻塞等待 epoll 返回结果
      result = ::epoll_wait(m_epoll_fd, event_list.get(), 64,
                            static_cast<int>(next_timeout));

      if (result < 0 /*&& errno == EINTR*/) {
        // TODO 处理 epoll_wait 异常
      }
      if (result >= 0) {
        break;
      }
    }

    // 处理定时器
    std::vector<Fiber::FiberFunc> fns;
    listExpiredCallback(fns);
    if (!fns.empty()) {
      schedule(fns.begin(), fns.end());
    }

    // 遍历 event_list 处理被触发事件的 fd
    for (int i = 0; i < result; i++) {
      epoll_event &ev = event_list[i];
      // 接收到来自主线程的消息
      if (ev.data.fd == m_tickle_fds[0]) {
        char dummy;
        // 将来自主线程的数据读取干净
        while (true) {
          int status = read(ev.data.fd, &dummy, 1);
          if (status == 0 || status == -1)
            break;
        }
        continue;
      }
      // 处理非主线程的消息
      auto fd_ctx = static_cast<FDContext *>(ev.data.ptr);
      ScopedLock lock(&fd_ctx->mutex);
      // 该事件的 fd 出现错误或者已经失效
      if (ev.events & (EPOLLERR | EPOLLHUP)) {
        ev.events |= EPOLLIN | EPOLLOUT;
      }
      uint32_t real_events = FDEvent::NONE;
      if (ev.events & EPOLLIN) {
        real_events |= FDEvent::READ;
      }
      if (ev.events & EPOLLOUT) {
        real_events |= FDEvent::WRITE;
      }
      // fd_ctx 中指定监听的事件都已经被触发并处理
      if ((fd_ctx->events & real_events) == FDEvent::NONE) {
        continue;
      }
      // 从 epoll 中移除这个 fd 的被触发的事件的监听
      uint32_t left_events = (fd_ctx->events & ~real_events);
      int op = left_events == 0 ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
      ev.events = EPOLLET | left_events;
      int rt = ::epoll_ctl(m_epoll_fd, op, fd_ctx->fd, &ev);
      // epoll 事件修改失败，打印一条 ERROR 日志，不做任何处理
      if (rt == -1) {
        LOG_FMT_ERROR(root_logger,
                      "epoll_ctl(%d, %d, %d, %ul) : return %d, errno = %d, %s",
                      m_epoll_fd, op, fd_ctx->fd, ev.events, rt, errno,
                      strerror(errno));
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
    // 让出当前线程的执行权，给调度器执行排队等待的协程
    // Fiber::YieldToHold();
    Fiber::sptr current_fiber = Fiber::GetCurrent();
    auto raw_ptr = current_fiber.get();
    current_fiber.reset();
    // raw_ptr->swapOut();
    raw_ptr->yield();
  }
}

void IOManager::onTimerInsertedAtFirst() { tickle(); }

/* -------------------------------- FDContext ------------------------------- */

void FDContext::addEvent(FDEvent event) {
  events = static_cast<FDEvent>(events | event);
}

void FDContext::cancelEvent(FDEvent event) {
  events = static_cast<FDEvent>(events & ~event);
}

void FDContext::triggerEvent(FDEvent event) {
  ASSERT(events & event);
  cancelEvent(event);
  auto &handler = getHandler(event);
  ASSERT(handler.scheduler);
  if (!handler.isEmpty()) {
    handler.scheduler->schedule(std::move(handler.handle));
  }
  handler.reset(nullptr, nullptr); // 从FDContext中删除该事件对应的信息
}

FDContext::EventHandler &FDContext::getHandler(FDEvent event) {
  switch (event) {
  case FDEvent::READ:
    return read_handler;
  case FDEvent::WRITE:
    return write_handler;
  default:
    throw std::invalid_argument("事件类型不正确");
  }
}

void FDContext::setHandler(FDEvent event, Scheduler *scheduler,
                           const Fiber::FiberFunc &callback) {
  auto &ev_handler = getHandler(event);
  ev_handler.reset(scheduler, callback);
}

void FDContext::clearHandler(FDContext::EventHandler &handler) {
  handler.reset(nullptr, nullptr);
}

} // namespace meha
