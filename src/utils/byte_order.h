#pragma once

#include <byteswap.h>
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace meha::utils
{

// 提供一些重载版本的bswap来使用

// @brief 将8字节整数进行字节序转换
template<typename Integer>
inline auto ByteSwap(Integer num) // REVIEW 这里auto会推导为什么？
{
    static_assert(std::is_integral_v<Integer>, "Integer must be integral type.");
    if constexpr (sizeof(Integer) == sizeof(uint64_t))
        return static_cast<Integer>(bswap_64(num));
    else if constexpr (sizeof(Integer) == sizeof(uint32_t))
        return static_cast<Integer>(bswap_32(num));
    else if constexpr (sizeof(Integer) == sizeof(uint16_t))
        return static_cast<Integer>(bswap_16(num));
    else {
        errno = EOVERFLOW;
        throw std::out_of_range("integer size is not supported.");
    }
}

// 以下是使用SFINAE的等价写法

// template <typename Integer>
// inline std::enable_if_t<sizeof(Integer) == sizeof(uint64_t)> ByteSwap(Integer num)
// {
//     static_assert(std::is_integral_v<Integer>, "Integer must be integral type.");
//     return static_cast<Integer>(bswap_64(num));
// }
// template <typename Integer>
// inline std::enable_if_t<sizeof(Integer) == sizeof(uint32_t)> ByteSwap(Integer num)
// {
//     static_assert(std::is_integral_v<Integer>, "Integer must be integral type.");
//     return static_cast<Integer>(bswap_32(num));
// }
// template <typename Integer>
// inline std::enable_if_t<sizeof(Integer) == sizeof(uint16_t)> ByteSwap(Integer num)
// {
//     static_assert(std::is_integral_v<Integer>, "Integer must be integral type.");
//     return static_cast<Integer>(bswap_16(num));
// }

/**
 * @brief 生成指定位数的子网掩码
 * @param bits 掩码的位数
 * sizeof(T) * 8: 算出有地址总共多少位
 * sizeof(T) * 8 - bits: 算出主机号有多少位
 * 1 << 主机号位数: 低位空出主机号的0
 * -1: 子网掩码部分都为0，后面的主机号部分都为1
 * ~: 将子网掩码部分置为1，后面的主机号部分置位0
 */
template<typename Integer>
inline Integer GenMask(uint32_t bits)
{
    static_assert(std::is_integral_v<Integer>, "Integer must be integral type.");
    return ~((1 << ((sizeof(Integer) * 8) - bits)) - 1);
}

/**
 * @brief 根据子网掩码对应的十进制数，计算子网掩码的位数
 */
template<typename Integer>
inline size_t CountBytes(Integer mask)
{
    static_assert(std::is_integral_v<Integer>, "Integer must be integral type.");
    size_t bits = 0;
    for (; mask; ++bits) {
        // 将最低位的1置为0
        mask &= mask - 1;
    }
    return bits;
}
} // namespace meha::utils::net