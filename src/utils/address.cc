#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

#include "address.h"
#include "byte_order.h"
#include "macro.h"
#include "module/log.h"


/*
NOTE 不要随便把函数中的临时变量搞成static的，这样容易导致函数不可重入
如下面的stringstream、sockaddr_in等，如果变成static的，那么一旦Address重置了，这些变量也需要重置，
但是这些变量不是成员，所以无法被通知重置。
*/

namespace meha
{
using namespace utils;

Address::sptr Address::Create(const sockaddr *const addr, const socklen_t addrlen)
{
    if (addr == nullptr || addrlen == 0)
        goto err;
    switch (addr->sa_family) {
    case AF_INET:
        if (addrlen == sizeof(sockaddr_in))
            return std::make_shared<IPv4Address>(*reinterpret_cast<const sockaddr_in *>(addr));
        else
            goto err;
    case AF_INET6:
        if (addrlen == sizeof(sockaddr_in6))
            return std::make_shared<IPv6Address>(*reinterpret_cast<const sockaddr_in6 *>(addr));
        else
            goto err;
    default:
        return std::make_shared<UnknowAddress>(addr->sa_family);
    }
err:
    return nullptr;
}

sa_family_t Address::family() const
{
    return address()->sa_family;
}

std::string Address::toString()
{
    std::stringstream ss;
    marshal(ss);
    return ss.str();
}

bool Address::operator<(const Address &rhs) const
{
    socklen_t min_len = std::min(addrLen(), rhs.addrLen());
    int result = std::memcmp(address(), rhs.address(), min_len);
    if (result) {
        return result < 0;
    } else {
        return addrLen() < rhs.addrLen();
    }
}

bool Address::operator==(const Address &rhs) const
{
    return (addrLen() == rhs.addrLen()) && !std::memcmp(address(), rhs.address(), addrLen());
}

bool Address::operator!=(const Address &rhs) const
{
    return !(*this == rhs);
}

IPv4Address::IPv4Address(uint32_t address, uint16_t port)
{
    std::memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family = AF_INET;
    m_addr.sin_addr.s_addr = htonl(address);
    m_addr.sin_port = htons(port);
}

IPv4Address::IPv4Address(const sockaddr_in &addr)
    : m_addr(addr)
{
}

IPv4Address::sptr IPv4Address::Create(const std::string &socket_str)
{
    auto pos = socket_str.rfind(':');
    if (pos == std::string::npos) {
        LOG_ERROR(core, "构造完整的Socket地址必须具有IP地址和端口号");
        return nullptr;
    } else {
        auto addr_str = socket_str.substr(0, pos);
        auto port_str = socket_str.substr(pos + 1);
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        int ret = inet_pton(AF_INET, addr_str.data(), &addr.sin_addr.s_addr);
        addr.sin_port = htons(std::atoi(port_str.data()));
        if (ret <= 0 || addr.sin_port <= 0) {
            LOG_FMT_ERROR(core, "IP地址或端口号解析错误: %s(%d)", std::strerror(errno), errno);
            return nullptr;
        }
        return std::make_shared<IPv4Address>(addr);
    }
}

// NOTE 由于sockaddr_in与sockaddr类型之间没有继承关系，这里用不了static_cast
const sockaddr *IPv4Address::address() const
{
    return reinterpret_cast<const sockaddr *>(&m_addr);
}

socklen_t IPv4Address::addrLen() const
{
    return sizeof(sockaddr_in);
}

IPAddress::sptr IPv4Address::broadcastAddress(uint32_t mask_bits)
{
    ASSERT_FMT(!(mask_bits > 32), "IPv4地址掩码不能超过32位");
    sockaddr_in baddr(m_addr); // 广播地址是主机号全为1
    baddr.sin_addr.s_addr |= ByteSwap(GenMask<uint32_t>(mask_bits));
    return std::make_shared<IPv4Address>(baddr);
}

IPAddress::sptr IPv4Address::networkAddress(uint32_t mask_bits)
{
    ASSERT_FMT(!(mask_bits > 32), "IPv4地址掩码不能超过32位");
    sockaddr_in naddr(m_addr);
    naddr.sin_addr.s_addr &= ~ByteSwap(GenMask<uint32_t>(mask_bits));
    return std::make_shared<IPv4Address>(naddr);
}

IPAddress::sptr IPv4Address::subnetMask(uint32_t mask_bits)
{
    ASSERT_FMT(!(mask_bits > 32), "IPv4地址掩码不能超过32位");
    sockaddr_in smask(m_addr);
    smask.sin_addr.s_addr = ByteSwap(GenMask<uint32_t>(mask_bits));
    return std::make_shared<IPv4Address>(smask);
}

uint32_t IPv4Address::port() const
{
    return ntohs(m_addr.sin_port);
}

void IPv4Address::setPort(uint16_t port)
{
    m_addr.sin_port = htons(port);
}

std::ostream &IPv4Address::marshal(std::ostream &os) const
{
    // NOTE 经典面试题：IPv4地址转换为十进制字符串
    uint32_t addr = ntohl(m_addr.sin_addr.s_addr);
    os << ((addr >> 24) & 0xff) << "." << ((addr >> 16) & 0xff) << "." << ((addr >> 8) & 0xff) << "." << ((addr >> 0) & 0xff) << ":"
       << ntohs(m_addr.sin_port);
    return os;
}

IPv6Address::IPv6Address(const char *address, uint16_t port)
{
    std::memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
    m_addr.sin6_port = htons(port);
    std::memcpy(&m_addr.sin6_addr.s6_addr, address, sizeof(m_addr.sin6_addr.s6_addr));
}

IPv6Address::IPv6Address(const sockaddr_in6 &addr)
    : m_addr(addr)
{
}

IPv6Address::sptr IPv6Address::Create(const std::string &socket_str)
{
    auto pos = socket_str.rfind(":");
    if (pos == std::string::npos) {
        LOG_ERROR(core, "构造完整的Socket地址必须具有IP地址和端口号");
        return nullptr;
    } else {
        auto addr_str = socket_str.substr(0, pos);
        auto port_str = socket_str.substr(pos + 1);
        sockaddr_in6 addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        int ret = inet_pton(AF_INET6, addr_str.data(), &addr.sin6_addr.s6_addr);
        addr.sin6_port = htons(std::atoi(port_str.data()));
        if (ret <= 0 || addr.sin6_port <= 0) {
            LOG_FMT_ERROR(core, "IP地址或端口号解析错误: %s(%d)", std::strerror(errno), errno);
            return nullptr;
        }
        return std::make_shared<IPv6Address>(addr);
    }
}

const sockaddr *IPv6Address::address() const
{
    return reinterpret_cast<const sockaddr *>(&m_addr);
}

socklen_t IPv6Address::addrLen() const
{
    return sizeof(sockaddr_in6);
}

IPAddress::sptr IPv6Address::broadcastAddress(uint32_t mask_bits)
{
    ASSERT_FMT(!(mask_bits > 128), "IPv6地址掩码不能超过128位");
    sockaddr_in6 baddr(m_addr); // IPv6地址本身128位太大了装不下，只能一截一截手动转
    // 1. 先处理最低字节，因为最后一字节的掩码可能不是完整的0xff
    baddr.sin6_addr.s6_addr[mask_bits / 8] |= GenMask<uint8_t>(mask_bits % 8);
    // 2. 接着处理剩余的字节
    for (int i = mask_bits / 8 + 1; i < 16; ++i) {
        baddr.sin6_addr.s6_addr[i] = 0xff;
    }
    return std::make_shared<IPv6Address>(baddr);
}

IPAddress::sptr IPv6Address::networkAddress(uint32_t mask_bits)
{
    ASSERT_FMT(!(mask_bits > 128), "IPv6地址掩码不能超过128位");
    sockaddr_in6 naddr(m_addr);
    naddr.sin6_addr.s6_addr[mask_bits / 8] &= ~GenMask<uint8_t>(mask_bits % 8);
    // for (int i = mask_bits / 8 + 1; i < 16; ++i) {
    //     naddr.sin6_addr.s6_addr[i] = 0x00;
    // }
    return std::make_shared<IPv6Address>(naddr);
}

IPAddress::sptr IPv6Address::subnetMask(uint32_t mask_bits)
{
    ASSERT_FMT(!(mask_bits > 128), "IPv6地址掩码不能超过128位");
    sockaddr_in6 subnet(m_addr);
    subnet.sin6_addr.s6_addr[mask_bits / 8] = GenMask<uint8_t>(mask_bits % 8);
    for (uint32_t i = 0; i < mask_bits / 8; ++i) {
        subnet.sin6_addr.s6_addr[i] = 0xff;
    }
    return std::make_shared<IPv6Address>(subnet);
}

uint32_t IPv6Address::port() const
{
    return ntohs(m_addr.sin6_port);
}

void IPv6Address::setPort(uint16_t port)
{
    m_addr.sin6_port = htons(port);
}

std::ostream &IPv6Address::marshal(std::ostream &os) const
{
    const uint16_t *addr = m_addr.sin6_addr.s6_addr16; // 按照2B为单位取出，这样每截就是两个十六进制的数，如FFFF
    bool used_zeros = false; // 确保地址中只能使用一次零压缩。这里的策略是第一次遇到0就使用零压缩，后续再遇到0就省略0为1个
    for (size_t i = 0; i < 8; i++) {
        if (!used_zeros) {
            if (addr[i] == 0)
                continue;
            else if (addr[i - 1] == 0) {
                os << ":";
                used_zeros = true;
            }
        }
        os << std::hex << ntohs(addr[i]);
        if (0 < i && i < 7) {
            os << ":";
        }
    }
    if (!used_zeros && addr[7] == 0) {
        os << ":";
    }
    os << std::dec << ntohs(m_addr.sin6_port);
    return os;
}

UnixAddress::UnixAddress(const std::string &path)
{
    ASSERT_FMT(path.size() < MAX_PATH_LEN, "UNIX套接字路径太长了");
    ASSERT_FMT(path != "", "UNIX套接字路径必须存在!"); // REVIEW 是否必须存在？
    std::memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    std::memmove(&m_addr.sun_path, path.data(), path.size());
    m_length = offsetof(sockaddr_un, sun_path) + path.size() + 1; // +1是为了补上字符数组末尾的终止符\0
}

const sockaddr *UnixAddress::address() const
{
    return reinterpret_cast<const sockaddr *>(&m_addr);
}

socklen_t UnixAddress::addrLen() const
{
    return m_length;
}

std::string UnixAddress::path() const
{
    return m_addr.sun_path;
}

void UnixAddress::setPath(const std::string &path)
{
    std::memset(&m_addr.sun_path, 0, sizeof(m_addr.sun_path));
    std::memmove(&m_addr.sun_path, path.data(), path.size());
}

std::ostream &UnixAddress::marshal(std::ostream &os) const
{
    return os << m_addr.sun_path;
}

UnknowAddress::UnknowAddress(sa_family_t family)
{
    std::memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sa_family = family;
}

const sockaddr *UnknowAddress::address() const
{
    return reinterpret_cast<const sockaddr *>(&m_addr);
}

socklen_t UnknowAddress::addrLen() const
{
    return sizeof(m_addr);
}

std::ostream &UnknowAddress::marshal(std::ostream &os) const
{
    os << "UnknownAddress family=" << m_addr.sa_family;
    return os;
}

} // namespace meha
