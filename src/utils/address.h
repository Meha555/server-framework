#pragma once

#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

namespace meha
{

/**
 * @brief 地址基类
 */
class Address
{
public:
    friend std::ostream &operator<<(std::ostream &os, const Address &addr);
    using sptr = std::shared_ptr<Address>;

    /**
     * @brief 便利静态函数：通过外部设置的sockaddr指针创建Address
     * @param[in] addr sockaddr指针
     * @param[in] addrlen sockaddr的长度
     * @return 返回和sockaddr相匹配的Address,失败返回nullptr
     */
    static Address::sptr Create(const sockaddr *const addr, const socklen_t addrlen);

    virtual ~Address() = default;

    // 获取协议族
    sa_family_t family() const;

    // 获取 socket 地址
    virtual const sockaddr *address() const = 0;

    // 获取地址长度（字节数）
    virtual socklen_t addrLen() const = 0;

    std::string toString();

    // 由于Address可能放在容器中，所以提供一些比较运算符
    bool operator<(const Address &rhs) const;
    bool operator==(const Address &rhs) const;
    bool operator!=(const Address &rhs) const;

protected:
    virtual std::ostream &marshal(std::ostream &os) const = 0;
};

/**
 * @brief IP 地址基类
 */
class IPAddress : public Address
{
public:
    using sptr = std::shared_ptr<IPAddress>;

    // 获取广播地址
    virtual IPAddress::sptr broadcastAddress(uint32_t mask_bits) = 0;

    // 获取网络地址
    virtual IPAddress::sptr networkAddress(uint32_t mask_bits) = 0;

    // 获取子网掩码
    virtual IPAddress::sptr subnetMask(uint32_t mask_bits) = 0;

    // 获取端口号
    virtual uint32_t port() const = 0;

    virtual void setPort(uint16_t port) = 0;
};

/**
 * @brief IPv4 地址信息类
 */
class IPv4Address : public IPAddress
{
public:
    using sptr = std::shared_ptr<IPv4Address>;

    explicit IPv4Address(uint32_t address = INADDR_ANY, uint16_t port = 0);
    explicit IPv4Address(const sockaddr_in &addr); // NOTE 这里是浅拷贝

    /**
     * @brief 根据地址字符串创建对象的辅助函数
     * @param socket_str marshal格式的字符串
     * @return sptr
     */
    static sptr Create(const std::string &socket_str);

    // 获取 socket 地址
    const sockaddr *address() const override;

    // 获取地址长度（字节数）
    socklen_t addrLen() const override;

    // 获取广播地址
    IPAddress::sptr broadcastAddress(uint32_t mask_bits) override;

    // 获取网络地址
    IPAddress::sptr networkAddress(uint32_t mask_bits) override;

    // 获取子网掩码
    IPAddress::sptr subnetMask(uint32_t mask_bits) override;

    // 获取端口号
    uint32_t port() const override;

    void setPort(uint16_t port) override;

protected:
    std::ostream &marshal(std::ostream &os) const override;

private:
    sockaddr_in m_addr;
};

/**
 * @brief IPv6 地址信息类
 */
class IPv6Address : public IPAddress
{
public:
    using sptr = std::shared_ptr<IPv6Address>;

    explicit IPv6Address(const char *address = "", uint16_t port = 0);
    explicit IPv6Address(const sockaddr_in6 &addr); // NOTE 这里是浅拷贝

    /**
     * @brief 根据地址字符串创建对象的辅助函数
     * @param socket_str marshal格式的字符串
     * @return sptr
     */
    static sptr Create(const std::string &socket_str);

    // 获取 socket 地址
    const sockaddr *address() const override;

    // 获取地址长度（字节数）
    socklen_t addrLen() const override;

    // 获取广播地址
    IPAddress::sptr broadcastAddress(uint32_t mask_bits) override;

    // 获取网络地址
    IPAddress::sptr networkAddress(uint32_t mask_bits) override;

    // 获取子网掩码
    IPAddress::sptr subnetMask(uint32_t mask_bits) override;

    // 获取端口号
    uint32_t port() const override;

    void setPort(uint16_t port) override;

protected:
    std::ostream &marshal(std::ostream &os) const override;

private:
    sockaddr_in6 m_addr;
};

/**
 * @brief Unix 地址信息类
 */
class UnixAddress : public Address
{
public:
    using sptr = std::shared_ptr<UnixAddress>;
    // REVIEW 这样->是不算解引用是吗？
    static constexpr size_t MAX_PATH_LEN = sizeof(((sockaddr_un *)nullptr)->sun_path) - 1; // -1是因为sun_path是字符数组，末尾终止符\0

    explicit UnixAddress(const std::string &path);

    // 获取 socket 地址
    const sockaddr *address() const override;

    // 获取地址长度（字节数）
    socklen_t addrLen() const override;

    // 获取UNIX域套接字地址
    std::string path() const;

    // 设置UNIX域套接字地址
    void setPath(const std::string &path);

protected:
    std::ostream &marshal(std::ostream &os) const override;

private:
    sockaddr_un m_addr;
    socklen_t m_length; // 由于UNIX套接字地址长度不固定，因此需要额外的成员来记录地址结构体总长度（字节数）
};

/**
 * @brief 未知地址信息类
 */
class UnknowAddress : public Address
{
public:
    using sptr = std::shared_ptr<UnknowAddress>;

    explicit UnknowAddress(sa_family_t family);

    // 获取 socket 地址
    const sockaddr *address() const override;

    // 获取地址长度（字节数）
    socklen_t addrLen() const override;

protected:
    std::ostream &marshal(std::ostream &os) const override;

private:
    sockaddr m_addr;
};

inline std::ostream &operator<<(std::ostream &os, const Address &addr)
{
    return addr.marshal(os);
}

} // namespace meha
