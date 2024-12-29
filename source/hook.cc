#include "hook.h"
#include "config.hpp"
#include "fd_manager.h"
#include "io_manager.h"
#include "log.h"
#include <cstdarg>
extern "C" {
#include <dlfcn.h>
}

namespace meha
{

static meha::Logger::ptr root_logger = GET_LOGGER("root");

static meha::ConfigItem<int>::ptr g_tcp_connect_timeout = meha::Config::Lookup("tcp.connect.timeout", 5000);

namespace hook
{

static thread_local bool t_hook_enabled = false;

#define DEAL_FUNC(DO) \
    DO(sleep)         \
    DO(usleep)        \
    DO(nanosleep)     \
    DO(socket)        \
    DO(connect)       \
    DO(accept)        \
    DO(recv)          \
    DO(recvfrom)      \
    DO(recvmsg)       \
    DO(send)          \
    DO(sendto)        \
    DO(sendmsg)       \
    DO(getsockopt)    \
    DO(setsockopt)    \
    DO(read)          \
    DO(write)         \
    DO(close)         \
    DO(readv)         \
    DO(writev)        \
    DO(fcntl)         \
    DO(ioctl)

static uint64_t s_connect_timeout = -1;
struct _HookIniter // 这种初始化类，在Qt插件中一般叫Factory
{
    _HookIniter()
    {
        hook_init();
        s_connect_timeout = g_tcp_connect_timeout->getValue();
        g_tcp_connect_timeout->addListener(
            [](const int &old_value, const int &new_value) {
                LOG_FMT_INFO(root_logger, "tcp connect timeout change from %d to %d", old_value, new_value);
                s_connect_timeout = new_value;
            });
    }

    // 初始化被hook函数的原函数指针（通过dlsym）
    static void hook_init()
    {
        static bool is_inited = false; // 通过一个静态变量来保证仅初始化一次
        if (is_inited)
            return;
        is_inited = true;
#define TRY_LOAD_HOOK_FUNC(name) \
    name##_f = (name##_func)dlsym(RTLD_NEXT, #name); // 一定要是 RTLD_NEXT
        // 利用宏获取指定的系统 api 的函数指针
        DEAL_FUNC(TRY_LOAD_HOOK_FUNC)
#undef TRY_LOAD_HOOK_FUNC
    }
};
static _HookIniter s_hook_initer; // 定义一个静态的_HookIniter 对象，由于静态对象的初始化发生在main()调用前，因此hoot_init()会在程序执行前开始调用，从而确保原函数指针在main()开始时是可用的

bool IsHookEnabled()
{
    return t_hook_enabled;
}

void SetHookEnable(bool flag)
{
    t_hook_enabled = flag;
}

// 用于跟踪定时器的状态
struct TimerInfo
{
    int timeouted = 0; // 表示定时器是否已经超时
};

// @brief 执行hook逻辑的代理函数
// @param fd 执行IO操作的fd
// @param func 被hook的原函数指针
// @param func_name 被hook的原函数名
// @param event 执行的IO操作
// @param fd_timeout_type 超时时间类型（读超时/写超时）
// @param args 其他参数
template<typename OriginFunc, typename... Args>
static ssize_t doIO(int fd, OriginFunc func, const char *func_name,
                    meha::FdContext::FdEvent event, meha::FileDescriptor::TimeoutType fd_timeout_type, Args &&...args)
{
    // 如果没有启用 hook，则直接调用系统函数
    if (!meha::hook::t_hook_enabled) {
        return func(fd, std::forward<Args>(args)...);
    }

    if (func_name) {
        LOG_FMT_TRACE(meha::root_logger, "doIO 代理执行系统函数 %s", func_name);
    }

    auto fdp = meha::FileDescriptorManager::Instance()->fetch(fd); // 为啥这里不是fetch(fd, false)呢
    if (!fdp) {
        return func(fd, std::forward<Args>(args)...);
    }

    if (fdp->isClosed()) {
        errno = EBADF;
        return -1;
    }
    // 如果不是socket fd，或者是用户设置了非阻塞的fd，则不允许被hook（后者是因为给libc设置O_NONBLOCK会在内部异步化，这里就不需要协程了）
    if (!fdp->isSocket() || fdp->userNonBlock()) {
        return func(fd, std::forward<Args>(args)...);
    }

    // 执行到此，说明需要执行hook后协程化的版本

    uint64_t timeout_ms = fdp->timeout(fd_timeout_type); // 获取fd上设置的超时
    auto timer_info = std::make_shared<TimerInfo>(); // 生成一个监控超时的对象备用
retry:
    // 执行IO操作
    ssize_t n = func(fd, std::forward<Args>(args)...); // NOTE 可见实现函数的功能还是需要调用原本的API来实现
    // 出现错误 EINTR，是因为系统 API 在阻塞等待状态下被其他的系统信号中断执行
    // 此处的解决办法就是重新调用这次系统 API
    while (n == -1 && errno == EINTR) {
        n = func(fd, std::forward<Args>(args)...);
    }
    // 出现错误 EAGAIN，是因为长时间未读到数据或者无法写入数据（即资源未就绪，注意这里是sysNONBLOCK的）
    // 需要把这个 fd 丢到当前 IOManager 里监听对应事件，等待事件触发后再返回执行
    if (n == -1 && errno == EAGAIN) {
        if (func_name) {
            LOG_FMT_DEBUG(meha::root_logger, "doIO(%s): 开始异步等待", func_name);
        }

        auto iom = meha::IOManager::GetCurrent();
        meha::Timer::sptr timer;
        std::weak_ptr<TimerInfo> timer_info_wp(timer_info);
        // 如果设置了超时时间，在指定时间后取消掉该 fd 的事件监听
        if (timeout_ms != static_cast<uint64_t>(-1)) {
            timer = iom->addConditionalTimer(
                timeout_ms,
                [timer_info_wp, fd, iom, event]() {
                    auto t = timer_info_wp.lock();
                    if (!t || t->timeouted) {
                        return;
                    }
                    t->timeouted = ETIMEDOUT;
                    iom->triggerEvent(fd, event);
                },
                timer_info_wp);
        }
        uint64_t now = 0;

        int rt = iom->subscribeEvent(fd, event);
        if (!rt) {
            if (func_name) {
                LOG_FMT_ERROR(meha::root_logger, "%s 添加事件监听失败(%d, %u)", func_name, fd, event);
            }
            if (timer) {
                timer->cancel();
            }
            return -1;
        }

        // 注册完事件回调后，立即yield让出执行权
        meha::Fiber::Yield();

        // 获得执行权说明事件已就绪
        if (timer) {
            timer->cancel();
        }
        if (timer_info->timeouted) {
            errno = timer_info->timeouted;
            return -1;
        }
        goto retry; // 继续执行下一次IO
    }
    return n;
}

int ConnectWithTimeout(int sockfd, const struct sockaddr *addr,
                       socklen_t addrlen, uint64_t timeout_ms)
{
    if (!meha::hook::t_hook_enabled) {
        return connect_f(sockfd, addr, addrlen);
    }
    auto fdp = meha::FileDescriptorManager::Instance()->fetch(sockfd);
    if (!fdp || fdp->isClosed()) {
        errno = EBADF;
        return -1;
    }
    if (!fdp->isSocket() || fdp->userNonBlock()) {
        return connect_f(sockfd, addr, addrlen);
    }
    int n = connect_f(sockfd, addr, addrlen);
    if (n == 0) {
        return 0;
    } else if (n != -1 || errno != EINPROGRESS) {
        return n;
    }
    /**
     * 调用 connect，非阻塞形式下会返回-1，但是 errno 被设为 EINPROGRESS，表明
     * connect 仍旧在进行还没有完成。下一步就需要为其添加写就绪监听。
     */
    auto iom = meha::IOManager::GetCurrent();
    meha::Timer::sptr timer;
    auto timer_info = std::make_shared<TimerInfo>();
    std::weak_ptr<TimerInfo> weak_timer_info(timer_info);

    if (timeout_ms != static_cast<uint64_t>(-1)) {
        timer = iom->addConditionalTimer(
            timeout_ms,
            [weak_timer_info, sockfd, iom]() {
                auto t = weak_timer_info.lock();
                if (!t || t->timeouted) {
                    return;
                }
                t->timeouted = ETIMEDOUT;
                iom->triggerEvent(sockfd, meha::FdContext::FdEvent::WRITE);
            },
            weak_timer_info);
    }
    // TODO 下面的逻辑要看一下
    int rt = iom->subscribeEvent(sockfd, meha::FdContext::FdEvent::WRITE);
    if (rt == 0) {
        meha::Fiber::Yield();
        if (timer) {
            timer->cancel();
        }
        if (timer_info->timeouted) {
            errno = timer_info->timeouted;
            return -1;
        }
    }
    if (rt == -1) {
        if (timer) {
            timer->cancel();
        }
        LOG_FMT_ERROR(meha::root_logger, "ConnectWithTimeout addEventListen(%d, WRITE) error", sockfd);
    }
    // 处理错误
    int error = 0;
    socklen_t len = sizeof(int);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
        return -1;
    }

    if (!error) {
        return 0;
    } else {
        errno = error;
        return -1;
    }
}

} // namespace hook
} // namespace meha

// 这些用于侵入式Hook的函数都不能是static的
// FIXME 如果有程序使用了这个工程打包的库，那么那个程序中想要调用原有的函数就必须调用name##_f的版本吧
// 这样一来如果该程序原先没有使用这个库，现在引入了这个库，那么就造成原来的name##函数被偷偷hook了，这
// 可能不是我想要的。是否能通过指定符号可见性来解决这个问题？
extern "C" {
// 定义系统 api 的函数指针的变量
#define DEF_ORIGIN_FUNC(name) name##_func name##_f = nullptr;
DEAL_FUNC(DEF_ORIGIN_FUNC)
#undef DEF_ORIGIN_FUNC
#undef DEAL_FUNC

//////// sys/unistd.h

/**
 * @brief hook 处理后的 sleep
 */
unsigned int sleep(unsigned int seconds)
{
    if (!meha::hook::t_hook_enabled) {
        return sleep_f(seconds);
    }
    meha::Fiber::sptr fiber = meha::Fiber::GetCurrent();
    auto iom = meha::IOManager::GetCurrent();
    ASSERT(iom);
    // 设置超时
    iom->addTimer(seconds * 1000, [iom, fiber]() {
        iom->schedule(fiber);
    });
    meha::Fiber::Yield();
    return 0;
}

/**
 * @brief hook 处理后的 usleep
 */
int usleep(useconds_t usec)
{
    if (!meha::hook::t_hook_enabled) {
        return usleep_f(usec);
    }
    meha::Fiber::sptr fiber = meha::Fiber::GetCurrent();
    auto iom = meha::IOManager::GetCurrent();
    ASSERT(iom);
    // 设置超时
    iom->addTimer(usec / 1000, [iom, fiber]() {
        iom->schedule(fiber);
    });
    meha::Fiber::Yield();
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!meha::hook::t_hook_enabled) {
        return nanosleep_f(req, rem);
    }
    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 / 1000;
    meha::Fiber::sptr fiber = meha::Fiber::GetCurrent();
    auto iom = meha::IOManager::GetCurrent();
    ASSERT(iom);
    // 设置超时
    iom->addTimer(timeout_ms, [iom, fiber]() {
        iom->schedule(fiber);
    });
    meha::Fiber::Yield();
    return 0;
}

//////// sys/socket.h

int socket(int domain, int type, int protocol)
{
    if (!meha::hook::t_hook_enabled) {
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    meha::FileDescriptorManager::Instance()->fetch(fd, false);
    return fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return meha::hook::ConnectWithTimeout(sockfd, addr, addrlen, meha::hook::s_connect_timeout);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    int fd = meha::hook::doIO(sockfd, accept_f, "accept",
                  meha::FdContext::FdEvent::READ,
                  meha::FileDescriptor::RecvTimeout, // REVIEW 这里的recvtimout和sendtimeout是怎么确定的？
                  addr, addrlen);
    meha::FileDescriptorManager::Instance()->fetch(fd, true);
    return fd;
}

ssize_t read(int fd, void *buf, size_t count)
{
    return meha::hook::doIO(fd, read_f, "read", meha::FdContext::FdEvent::READ, meha::FileDescriptor::RecvTimeout, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    return meha::hook::doIO(fd, readv_f, "readv", meha::FdContext::FdEvent::READ, meha::FileDescriptor::RecvTimeout, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return meha::hook::doIO(sockfd, recv_f, "recv", meha::FdContext::FdEvent::READ, meha::FileDescriptor::RecvTimeout, buf,
                len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen)
{
    return meha::hook::doIO(sockfd, recvfrom_f, "recvfrom", meha::FdContext::FdEvent::READ, meha::FileDescriptor::RecvTimeout,
                buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return meha::hook::doIO(sockfd, recvmsg_f, "recvfrom", meha::FdContext::FdEvent::READ, meha::FileDescriptor::RecvTimeout,
                msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return meha::hook::doIO(fd, write_f, "write", meha::FdContext::FdEvent::WRITE, meha::FileDescriptor::SendTimeout, buf,
                count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    return meha::hook::doIO(fd, writev_f, "writev_f", meha::FdContext::FdEvent::WRITE, meha::FileDescriptor::SendTimeout, iov,
                iovcnt);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    return meha::hook::doIO(sockfd, send_f, "send", meha::FdContext::FdEvent::WRITE, meha::FileDescriptor::SendTimeout, buf,
                len, flags);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return meha::hook::doIO(sockfd, sendto_f, "sendto", meha::FdContext::FdEvent::WRITE, meha::FileDescriptor::SendTimeout,
                buf, len, flags, dest_addr, addrlen);
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return meha::hook::doIO(sockfd, sendmsg_f, "sendmsg", meha::FdContext::FdEvent::WRITE, meha::FileDescriptor::SendTimeout,
                msg, flags);
}

int close(int fd)
{
    if (!meha::hook::t_hook_enabled) {
        return close_f(fd);
    }

    meha::FileDescriptor::sptr fdp = meha::FileDescriptorManager::Instance()->fetch(fd);
    if (fdp) {
        auto iom = meha::IOManager::GetCurrent();
        if (iom) {
            iom->triggerAllEvents(fd);
        }
        meha::FileDescriptorManager::Instance()->remove(fd);
    }
    return close_f(fd);
}

int fcntl(int fd, int cmd, ... /* arg */)
{
    if (!meha::hook::t_hook_enabled) {
        va_list args;
        va_start(args, cmd);
        int res = fcntl_f(fd, cmd, args);
        va_end(args);
        return res;
    }

    va_list va;
    va_start(va, cmd);
    switch (cmd) {
    case F_SETFL: {
        int arg = va_arg(va, int);
        va_end(va);
        auto fdp = meha::FileDescriptorManager::Instance()->fetch(fd);
        if (!fdp || fdp->isClosed() || !fdp->isSocket()) {
            return fcntl_f(fd, cmd, arg);
        }
        // 更新其用户设置的非阻塞的标记状态
        fdp->setUserNonBlock(arg & O_NONBLOCK);
        // 获取其完整的 FD 状态标志
        if (fdp->systemNonBlock()) {
            arg |= O_NONBLOCK;
        } else {
            arg &= ~O_NONBLOCK;
        }
        return fcntl_f(fd, cmd, arg);
    } break;

    case F_GETFL: {
        va_end(va);
        int arg = fcntl_f(fd, cmd);
        auto fdp = meha::FileDescriptorManager::Instance()->fetch(fd);
        if (!fdp || fdp->isClosed() || !fdp->isSocket()) {
            return arg;
        }
        if (fdp->userNonBlock()) {
            return arg | O_NONBLOCK;
        } else {
            return arg & ~O_NONBLOCK;
        }
    } break;

    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    case F_SETFD:
    case F_SETOWN:
    case F_SETSIG:
    case F_SETLEASE:
    case F_NOTIFY:
    case F_SETPIPE_SZ: {
        int arg = va_arg(va, int);
        va_end(va);
        return fcntl_f(fd, cmd, arg);
    } break;

    case F_GETFD:
    case F_GETOWN:
    case F_GETSIG:
    case F_GETLEASE:
    case F_GETPIPE_SZ: {
        va_end(va);
        return fcntl_f(fd, cmd);
    } break;

    case F_SETLK:
    case F_SETLKW:
    case F_GETLK: {
        auto arg = va_arg(va, flock *);
        va_end(va);
        return fcntl_f(fd, cmd, arg);
    } break;

    case F_GETOWN_EX:
    case F_SETOWN_EX: {
        auto arg = va_arg(va, f_owner_ex *);
        va_end(va);
        return fcntl_f(fd, cmd, arg);
    } break;

    default:
        va_end(va);
        return fcntl_f(fd, cmd);
    }
}

int ioctl(int fd, unsigned long request, ...)
{
    // TODO 这个函数写的对吗？
    va_list va;
    va_start(va, request);
    void *arg = va_arg(va, void *);
    va_end(va);

    if (FIONBIO == request) {
        bool user_nonblock = !!*(int *)arg;
        auto fdp = meha::FileDescriptorManager::Instance()->fetch(fd);
        if (!fdp || fdp->isClosed() || !fdp->isSocket()) {
            return ioctl_f(fd, request, arg);
        }
        fdp->setUserNonBlock(user_nonblock);
    }
    return ioctl(fd, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval,
               socklen_t *optlen)
{
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval,
               socklen_t optlen)
{
    if (!meha::hook::t_hook_enabled) {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }

    if (level == SOL_SOCKET) {
        if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            auto fdp = meha::FileDescriptorManager::Instance()->fetch(sockfd);
            if (fdp) {
                const timeval *v = static_cast<const timeval *>(optval);
                fdp->setTimeout(static_cast<meha::FileDescriptor::TimeoutType>(optname), v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

} // extern "C"
