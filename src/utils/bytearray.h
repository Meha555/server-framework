#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/types.h>

#include "byteorder.h"

namespace meha::utils
{

/**
 * @brief 二进制数组,提供基础类型的序列化,反序列化功能
 */
class ByteArray
{
public:
    using sptr = std::shared_ptr<ByteArray>;

    /**
     * @brief ByteArray的存储节点
     */
    struct Node
    {
        /**
         * @brief 构造指定大小的内存块
         * @param[in] s 内存块字节数
         */
        explicit Node(size_t s);

        explicit Node();

        ~Node();

        /// 内存块地址指针
        char *ptr;
        /// 下一个内存块地址
        Node *next;
        /// 内存块大小
        size_t size;
    };

    /**
     * @brief 使用指定长度的内存块构造ByteArray
     * @param[in] block_size 内存块大小
     */
    explicit ByteArray(size_t block_size = 4096);

    ~ByteArray();

    /**
     * @brief 写入固定长度有符号Integer类型的数据
     * @post m_pos += sizeof(value)
     *       如果 m_pos > m_size 则 m_size = m_pos
     */
    template<size_t N, typename SignedInteger, typename Dummy = std::enable_if_t<N == 8 || N == 16 || N == 32 || N == 64>>
    void writeFixedInt(SignedInteger value)
    {
        writeFixedIntImpl<N>(static_cast<std::conditional_t<N == 8, int8_t, std::conditional_t<N == 16, int16_t, std::conditional_t<N == 32, int32_t, std::conditional_t<N == 64, int64_t, void>>>>>(value));
    }

    /**
     * @brief 写入固定长度无符号Integer类型的数据
     * @post m_pos += sizeof(value)
     *       如果 m_pos > m_size 则 m_size = m_pos
     */
    template<size_t N, typename UnsignedInteger, typename Dummy = std::enable_if_t<N == 8 || N == 16 || N == 32 || N == 64>>
    void writeFixedUint(UnsignedInteger value)
    {
        writeFixedIntImpl<N>(static_cast<std::conditional_t<N == 8, uint8_t, std::conditional_t<N == 16, uint16_t, std::conditional_t<N == 32, uint32_t, std::conditional_t<N == 64, uint64_t, void>>>>>(value));
    }

    /**
     * @brief 写入有符号Integer类型的Variant编码数据
     * @param SignedInteger 有符号Integer类型（int32_t或int64_t）
     * @param N 编码方式(32或64)
     * @note 编码方式是：Variant32或Variant64
     * @post m_pos += 实际占用内存(1 ~ 5或10 bytes)
     *       如果 m_pos > m_size 则 m_size = m_pos
     */
    template<size_t N, typename SignedInteger = std::conditional<N == 32, int32_t, std::conditional<N == 64, int64_t, void>>, typename Dummy = std::enable_if_t<std::is_signed_v<SignedInteger>>>
    void writeVariantInt(SignedInteger value)
    {
        writeVariantUint<N>(Encode::Zigzag(value));
    }

    /**
     * @brief 写入无符号Integer类型的Variant编码数据
     * @param UnsignedInteger 无符号Integer类型（uint32_t或uint64_t）
     * @param N 编码方式(32或64)
     * @note 编码方式是：Variant32或Variant64
     * @post m_pos += 实际占用内存(1 ~ 5或10 bytes)
     *       如果 m_pos > m_size 则 m_size = m_pos
     */
    template<size_t N, typename UnsignedInteger = std::conditional<N == 32, uint32_t, std::conditional<N == 64, uint64_t, void>>, typename Dummy = std::enable_if_t<std::is_unsigned_v<UnsignedInteger>>>
    void writeVariantUint(UnsignedInteger value)
    {
        size_t bytes = N == 32 ? 5 : 10;
        uint8_t tmp[bytes];
        uint8_t i = 0;
        while (value >= 0x80) {
            tmp[i++] = (value & 0x7F) | 0x80;
            value >>= 7;
        }
        tmp[i++] = value;
        write(tmp, i);
    }

    /**
     * @brief 写入float类型的数据
     * @post m_pos += sizeof(value)
     *       如果 m_pos > m_size 则 m_size = m_pos
     */
    void writeFloat(float value);

    /**
     * @brief 写入double类型的数据
     * @post m_pos += sizeof(value)
     *       如果 m_pos > m_size 则 m_size = m_pos
     */
    void writeDouble(double value);

    /**
     * @brief 写入std::string类型的数据,用uintN_t作为长度类型
     * @post m_pos += 2 + value.size()
     *       如果 m_pos > m_size 则 m_size = m_pos
     */
    template<size_t N, typename Dummy = std::enable_if_t<N == 16 || N == 32 || N == 64>>
    void writeFixedString(const std::string &value)
    {
        writeFixedUint<N>(value.size());
        write(value.c_str(), value.size());
    }

    /**
     * @brief 写入std::string类型的数据,用无符号Varint64作为长度类型
     * @post m_pos += Varint64长度 + value.size()
     *       如果 m_pos > m_size 则 m_size = m_pos
     */
    void writeStringVariantInt(const std::string &value);

    /**
     * @brief 写入std::string类型的数据,无长度
     * @post m_pos += value.size()
     *       如果 m_pos > m_size 则 m_size = m_pos
     */
    void writeStringWithoutLength(const std::string &value);

    /**
     * @brief 读取固定长度有符号Integer类型的数据
     * @pre readableSize() >= sizeof(SignedInteger)
     * @post m_pos += sizeof(SignedInteger);
     * @exception 如果 readableSize() < sizeof(SignedInteger) 抛出 std::out_of_range
     */
    template<size_t N, typename SignedInteger = std::conditional_t<N == 8, int8_t, std::conditional_t<N == 16, int16_t, std::conditional_t<N == 32, int32_t, std::conditional_t<N == 64, int64_t, void>>>>>
    std::enable_if_t<std::is_integral_v<SignedInteger>, SignedInteger>
    readFixedInt()
    {
        return readFixedIntImpl<SignedInteger>();
    }

    /**
     * @brief 读取固定长度无符号Integer类型的数据
     * @pre readableSize() >= sizeof(UnsignedInteger)
     * @post m_pos += sizeof(UnsignedInteger);
     * @exception 如果 readableSize() < sizeof(UnsignedInteger) 抛出 std::out_of_range
     */
    template<size_t N, typename UnsignedInteger = std::conditional_t<N == 8, uint8_t, std::conditional_t<N == 16, uint16_t, std::conditional_t<N == 32, uint32_t, std::conditional_t<N == 64, uint64_t, void>>>>>
    std::enable_if_t<std::is_integral_v<UnsignedInteger>, UnsignedInteger>
    readFixedUint()
    {
        return readFixedIntImpl<UnsignedInteger>();
    }

    /**
     * @brief 读取有符号VarintN类型的数据
     * @pre readableSize() >= 有符号VarintN实际占用内存
     * @post m_pos += 有符号VarintN实际占用内存
     * @exception 如果 readableSize() < 有符号VarintN实际占用内存 抛出 std::out_of_range
     */
    template<size_t N, typename Dummy = std::enable_if_t<N == 32 || N == 64>>
    auto readVariantInt()
    {
        return Decode::Zigzag(readVariantUint<N>());
    }
    /**
     * @brief 读取无符号VarintN类型的数据
     * @pre readableSize() >= 无符号VarintN实际占用内存
     * @post m_pos += 无符号VarintN实际占用内存
     * @exception 如果 readableSize() < 无符号VarintN实际占用内存 抛出 std::out_of_range
     */
    template<size_t N, typename UnsignedInteger = std::conditional_t<N == 32, uint32_t, std::conditional_t<N == 64, uint64_t, void>>, typename Dummy = std::enable_if_t<(N == 32 || N == 64) && std::is_unsigned_v<UnsignedInteger>>>
    UnsignedInteger readVariantUint()
    {
        UnsignedInteger result = 0;
        for (int i = 0; i < N; i += 7) {
            uint8_t b = readFixedUint<8>();
            if (b < 0x80) {
                result |= ((UnsignedInteger)b) << i;
                break;
            } else {
                result |= (((UnsignedInteger)(b & 0x7f)) << i);
            }
        }
        return result;
    }

    /**
     * @brief 读取float类型的数据
     * @pre readableSize() >= sizeof(float)
     * @post m_pos += sizeof(float);
     * @exception 如果readableSize() < sizeof(float) 抛出 std::out_of_range
     */
    float readFloat();

    /**
     * @brief 读取double类型的数据
     * @pre readableSize() >= sizeof(double)
     * @post m_pos += sizeof(double);
     * @exception 如果readableSize() < sizeof(double) 抛出 std::out_of_range
     */
    double readDouble();

    /**
     * @brief 读取std::string类型的数据,用uintN_t作为长度类型
     * @pre readableSize() >= sizeof(uintN_t) + size
     * @post m_pos += sizeof(uintN_t) + size;
     * @exception 如果 readableSize() < sizeof(uintN_t) + size 抛出 std::out_of_range
     */
    template<size_t N, typename Dummy = std::enable_if_t<N == 16 || N == 32 || N == 64>>
    std::string readFixedString()
    {
        auto len = readFixedUint<N>();
        std::string buff;
        buff.resize(len);
        read(&buff[0], len);
        return buff;
    }

    /**
     * @brief 读取std::string类型的数据,用无符号Varint64作为长度
     * @pre readableSize() >= 无符号Varint64实际大小 + size
     * @post m_pos += 无符号Varint64实际大小 + size;
     * @exception 如果 readableSize() < 无符号Varint64实际大小 + size 抛出 std::out_of_range
     */
    std::string readStringVariantInt();

    /**
     * @brief 清空ByteArray
     * @post m_pos = 0, m_size = 0
     */
    void clear();

    /**
     * @brief 写入size长度的数据
     * @param[in] buf 内存缓存指针
     * @param[in] size 数据大小
     * @post m_pos += size, 如果 m_pos > m_size 则 m_size = m_pos
     */
    void write(const void *buf, size_t size);

    /**
     * @brief 读取size长度的数据
     * @param[out] buf 内存缓存指针
     * @param[in] size 数据大小
     * @post m_pos += size, 如果 m_pos > m_size 则 m_size = m_pos
     * @exception 如果readableSize() < size 则抛出 std::out_of_range
     */
    void read(void *buf, size_t size);

    /**
     * @brief 读取size长度的数据
     * @param[out] buf 内存缓存指针
     * @param[in] size 数据大小
     * @param[in] position 读取开始位置
     * @exception 如果 (m_size - position) < size 则抛出 std::out_of_range
     */
    void read(void *buf, size_t size, size_t position) const;

    /**
     * @brief 设置ByteArray当前位置
     * @post 如果 m_pos > m_size 则 m_size = m_pos
     * @exception 如果 m_pos > m_capacity 则抛出 std::out_of_range
     */
    void seek(size_t v);

    /**
     * @brief 把ByteArray的数据写入到文件中
     * @param[in] name 文件名
     */
    bool writeToFile(const std::string &name) const;

    /**
     * @brief 从文件中读取数据
     * @param[in] name 文件名
     */
    bool readFromFile(const std::string &name);

    /**
     * @brief 返回ByteArray当前位置
     */
    size_t pos() const
    {
        return m_pos;
    }

    /**
     * @brief 返回内存块的大小
     */
    size_t blockSize() const
    {
        return m_blockSize;
    }

    /**
     * @brief 返回可读取数据大小
     */
    size_t readableSize() const
    {
        return m_size - m_pos;
    }

    /**
     * @brief 返回数据的长度
     */
    size_t size() const
    {
        return m_size;
    }

    /**
     * @brief 是否是小端
     */
    bool isLittleEndian() const;

    /**
     * @brief 设置是否为小端
     */
    void asLittleEndian(bool val);

    /**
     * @brief 将ByteArray里面的数据[m_pos, m_size)转成std::string
     */
    std::string toString() const;

    /**
     * @brief 将ByteArray里面的数据[m_pos, m_size)转成16进制的std::string(格式:FF FF FF)
     */
    std::string toHexString() const;

    /**
     * @brief 获取可读取的缓存,保存成iovec数组
     * @param[out] buffers 保存可读取数据的iovec数组
     * @param[in] len 读取数据的长度,如果len > readableSize() 则 len = readableSize()
     * @return 返回实际数据的长度
     */
    uint64_t getReadBuffers(std::vector<iovec> &buffers, uint64_t len = ~0ull) const;

    /**
     * @brief 获取可读取的缓存,保存成iovec数组,从position位置开始
     * @param[out] buffers 保存可读取数据的iovec数组
     * @param[in] len 读取数据的长度,如果len > readableSize() 则 len = readableSize()
     * @param[in] position 读取数据的位置
     * @return 返回实际数据的长度
     */
    uint64_t getReadBuffers(std::vector<iovec> &buffers, uint64_t len, uint64_t position) const;

    /**
     * @brief 获取可写入的缓存,保存成iovec数组
     * @param[out] buffers 保存可写入的内存的iovec数组
     * @param[in] len 写入的长度
     * @return 返回实际的长度
     * @post 如果(m_pos + len) > m_capacity 则 m_capacity扩容N个节点以容纳len长度
     */
    uint64_t getWriteBuffers(std::vector<iovec> &buffers, uint64_t len);

private:
    /**
     * @brief 写入固定长度Integer类型的数据
     * @pre readableSize() >= sizeof(Integer)
     * @post m_pos += sizeof(Integer);
     * @exception 如果 readableSize() < sizeof(Integer) 抛出 std::out_of_range
     */
    template<size_t N, typename Integer
        , typename Dummy = std::enable_if_t<N == 8 || N == 16 || N == 32 || N == 64>>
    void writeFixedIntImpl(Integer value)
    {
        if (m_endian != BYTE_ORDER) {
            value = ByteSwap(value);
        }
        write(&value, N >> 3);
    }

    /**
     * @brief 读取固定长度Integer类型的数据
     * @pre readableSize() >= sizeof(Integer)
     * @post m_pos += sizeof(Integer);
     * @exception 如果 readableSize() < sizeof(Integer) 抛出 std::out_of_range
     */
    template<typename Integer>
    std::enable_if_t<std::is_integral_v<Integer>, Integer>
    readFixedIntImpl()
    {
        Integer value;
        read(&value, sizeof(Integer));
        return m_endian == BYTE_ORDER ? value : ByteSwap(value);
    }

    /**
     * @brief 扩容ByteArray,使其可以容纳size个数据(如果原本可以可以容纳,则不扩容)
     */
    void enlarge(size_t size);

    /**
     * @brief 获取当前的可写入容量
     */
    size_t capacity() const
    {
        return m_capacity - m_pos;
    }

    struct Encode // 使用重载来区分
    {
        static uint32_t Zigzag(const int32_t &v);
        static uint64_t Zigzag(const int64_t &v);
    };
    struct Decode // 使用重载来区分
    {
        static int32_t Zigzag(const uint32_t &v);
        static int64_t Zigzag(const uint64_t &v);
    };

private:
    /// 内存块的大小
    size_t m_blockSize;
    /// 当前操作位置
    size_t m_pos;
    /// 当前的总容量
    size_t m_capacity;
    /// 当前数据的大小
    size_t m_size;
    /// 字节序,默认大端
    int16_t m_endian;
    /// 第一个内存块指针
    Node *m_root;
    /// 当前操作的内存块指针
    Node *m_cur;
};

} // namespace meha::utils