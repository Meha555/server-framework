#include "fd_manager.h"
#include "hook.h"
#include <sys/stat.h>
#include <sys/types.h>

namespace meha
{

FileDescriptor::FileDescriptor(int fd)
    : m_state{false, false, false, false}
    , m_fd(fd)
    , m_recvTimeout(-1)
    , m_sendTimeout(-1)
    , m_iom(nullptr)
{
    init();
}

void FileDescriptor::init()
{
    struct stat fd_stat;
    if (fstat(m_fd, &fd_stat) == -1) {
        m_state.isSocket = false;
    } else {
        m_state.isSocket = S_ISSOCK(fd_stat.st_mode);
    }

    if (isSocket()) {
        // 强制 socket 文件描述符为非阻塞模式
        int flags = fcntl_f(m_fd, F_GETFL, 0); // NOTE 注意这里要调用hook前的原API
        fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
        m_state.sysNONBLOCK = true; // NOTE 框架内部设置O_NONBLOCK也属于系统设置非阻塞
    } else {
        m_state.sysNONBLOCK = false;
    }
    m_state.userNONBLOCK = false;
    m_state.isClosed = false;
}

void FileDescriptor::setTimeout(TimeoutType type, uint64_t v)
{
    if (type == RecvTimeout) {
        m_recvTimeout = v;
    } else if (type == SendTimeout) {
        m_sendTimeout = v;
    } else {
        ASSERT_FMT(false, "未知的超时类型：%d", type);
    }
}

uint64_t FileDescriptor::timeout(TimeoutType type)
{
    if (type == RecvTimeout) {
        return m_recvTimeout;
    } else if (type == SendTimeout) {
        return m_sendTimeout;
    } else {
        ASSERT_FMT(false, "未知的超时类型：%d", type);
    }
}

FileDescriptorManagerImpl::FileDescriptorManagerImpl()
{
    m_fdPool.resize(64);
}

FileDescriptor::sptr FileDescriptorManagerImpl::fetch(int fd, bool only_if_exists)
{
    if (fd < 0) {
        return nullptr;
    }
    ReadScopedLock rlock(&m_lock);
    if (m_fdPool.size() > static_cast<size_t>(fd)) {
        if (m_fdPool[fd] || only_if_exists) {
            return m_fdPool[fd];
        }
    } else {
        if (only_if_exists) {
            return nullptr;
        }
    }
    rlock.unlock();

    WriteScopedLock wlock(&m_lock);
    if (m_fdPool.size() <= fd) {
        m_fdPool.resize(fd << 1);
    }
    auto fdp = std::make_shared<FileDescriptor>(fd);
    m_fdPool[fd] = fdp;
    return fdp;
}

void FileDescriptorManagerImpl::remove(int fd)
{
    WriteScopedLock lock(&m_lock);
    if (m_fdPool.size() <= static_cast<size_t>(fd)) {
        return;
    }
    m_fdPool[fd].reset();
}

} // namespace meha
