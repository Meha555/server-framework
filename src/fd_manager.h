#pragma once

#include "io_manager.h"
#include "utils/singleton.h"
#include <memory>
extern "C" {
#include <sys/socket.h>
}

namespace meha
{

/**
 * @brief FileDescriptor 文件描述符包装类
 * @details 是否socket、是否阻塞、是否关闭、读写超时时间
 */
class FileDescriptor : public std::enable_shared_from_this<FileDescriptor>
{
public:
    using sptr = std::shared_ptr<FileDescriptor>;

    FileDescriptor(int fd);

    bool isSocket() const
    {
        return m_state.isSocket;
    };
    bool isClosed() const
    {
        return m_state.isClosed;
    };
    // 标记用户手动设置了O_NONBLOCK
    void setUserNonBlock(bool v)
    {
        m_state.userNONBLOCK = v;
    }
    // 用户是否手动设置了O_NONBLOCK
    bool userNonBlock() const
    {
        return m_state.userNONBLOCK;
    }
    // 标记系统是否设置了O_NONBLOCK
    void setSyetemNonBlock(bool v)
    {
        m_state.sysNONBLOCK = v;
    }
    // 系统是否设置了O_NONBLOCK
    bool systemNonBlock() const
    {
        return m_state.sysNONBLOCK;
    }

    enum TimeoutType {
        RecvTimeout = SO_RCVTIMEO, // 读超时
        SendTimeout = SO_SNDTIMEO, // 写超时
    };

    void setTimeout(TimeoutType type, uint64_t v);
    uint64_t timeout(TimeoutType type);

private:
    // 尝试将fd设置为非阻塞的，从而异步化
    void init();

    bool m_isInited;
    int m_fd;
    struct State
    {
        bool isSocket : 1;
        bool sysNONBLOCK : 1;
        bool userNONBLOCK : 1;
        bool isClosed : 1;
    } m_state;

    uint64_t m_recvTimeout;
    uint64_t m_sendTimeout;

    meha::IOManager *m_iom; // 改成const指针或者std::weak_ptr
};

/**
 * @brief FileDescriptorManagerImpl 文件描述符管理类
 */
class FileDescriptorManagerImpl
{
public:
    FileDescriptorManagerImpl();

    /**
     * @brief 获取文件描述符 fd 对应的包装对象，若指定参数 only_if_exists 为 false, 当该包装对象不在管理类中时，自动创建一个新的包装对象
     * @return 返回指定的文件描述符的包装对象；当指定的文件描述符不存在时，返回 nullptr，如果指定 only_if_exists 为 false，则为这个文件描述符创建新的包装对象并返回。
     */
    FileDescriptor::sptr fetch(int fd, bool only_if_exists = true);

    /**
     * @brief 将一个文件描述符从管理类中删除
     * //TODO 这里不需要用引用计数管理吗？
     */
    void remove(int fd);

private:
    RWMutex m_lock{};
    std::vector<FileDescriptor::sptr> m_fdPool; // 文件描述符池
};

using FileDescriptorManager = SingletonPtr<FileDescriptorManagerImpl>;

} // namespace meha
