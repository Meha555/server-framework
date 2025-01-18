#include "bytearray.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string.h>

#include "module/log.h"

namespace meha::utils
{

ByteArray::Node::Node(size_t s)
    : ptr(new char[s])
    , next(nullptr)
    , size(s)
{
}

ByteArray::Node::Node()
    : ptr(nullptr)
    , next(nullptr)
    , size(0)
{
}

ByteArray::Node::~Node()
{
    if (ptr) {
        delete[] ptr;
    }
}

ByteArray::ByteArray(size_t base_size)
    : m_blockSize(base_size)
    , m_pos(0)
    , m_capacity(base_size)
    , m_size(0)
    , m_endian(BIG_ENDIAN)
    , m_root(new Node(base_size))
    , m_cur(m_root)
{
}

ByteArray::~ByteArray()
{
    // 头删法
    Node *tmp = m_root;
    while (tmp) {
        m_cur = tmp;
        tmp = tmp->next;
        delete m_cur;
    }
}

bool ByteArray::isLittleEndian() const
{
    return m_endian == LITTLE_ENDIAN;
}

void ByteArray::asLittleEndian(bool val)
{
    if (val) {
        m_endian = LITTLE_ENDIAN;
    } else {
        m_endian = BIG_ENDIAN;
    }
}

uint32_t ByteArray::Encode::Zigzag(const int32_t &v)
{
    if (v < 0) {
        return ((uint32_t)(-v)) * 2 - 1;
    } else {
        return v * 2;
    }
}

uint64_t ByteArray::Encode::Zigzag(const int64_t &v)
{
    if (v < 0) {
        return ((uint64_t)(-v)) * 2 - 1;
    } else {
        return v * 2;
    }
}

int32_t ByteArray::Decode::Zigzag(const uint32_t &v)
{
    return (v >> 1) ^ -(v & 1);
}

int64_t ByteArray::Decode::Zigzag(const uint64_t &v)
{
    return (v >> 1) ^ -(v & 1);
}

void ByteArray::writeFloat(float value)
{
    uint32_t v;
    memcpy(&v, &value, sizeof(value));
    writeFixedUint<32>(v);
}

void ByteArray::writeDouble(double value)
{
    uint64_t v;
    memcpy(&v, &value, sizeof(value));
    writeFixedUint<64>(v);
}

void ByteArray::writeStringVarintInt(const std::string &value)
{
    writeVarintUint<64>(value.size());
    write(value.c_str(), value.size());
}

void ByteArray::writeStringWithoutLength(const std::string &value)
{
    write(value.c_str(), value.size());
}

float ByteArray::readFloat()
{
    uint32_t v = readFixedUint<32>();
    float value;
    memcpy(&value, &v, sizeof(v));
    return value;
}

double ByteArray::readDouble()
{
    uint64_t v = readFixedUint<64>();
    double value;
    memcpy(&value, &v, sizeof(v));
    return value;
}

std::string ByteArray::readStringVarintInt()
{
    uint64_t len = readVarintUint<64>();
    std::string buff;
    buff.resize(len);
    read(&buff[0], len);
    return buff;
}

void ByteArray::clear()
{
    m_pos = m_size = 0;
    m_capacity = m_blockSize;
    Node *tmp = m_root->next;
    while (tmp) {
        m_cur = tmp;
        tmp = tmp->next;
        delete m_cur;
    }
    m_cur = m_root;
    m_root->next = NULL;
}

void ByteArray::write(const void *buf, size_t size)
{
    if (size == 0) {
        return;
    }
    enlarge(size);

    size_t npos = m_pos % m_blockSize;
    size_t ncap = m_cur->size - npos;
    size_t bpos = 0;

    while (size > 0) {
        if (ncap >= size) {
            memcpy(m_cur->ptr + npos, (const char *)buf + bpos, size);
            if (m_cur->size == (npos + size)) {
                m_cur = m_cur->next;
            }
            m_pos += size;
            bpos += size;
            size = 0;
        } else {
            memcpy(m_cur->ptr + npos, (const char *)buf + bpos, ncap);
            m_pos += ncap;
            bpos += ncap;
            size -= ncap;
            m_cur = m_cur->next;
            ncap = m_cur->size;
            npos = 0;
        }
    }

    if (m_pos > m_size) {
        m_size = m_pos;
    }
}

void ByteArray::read(void *buf, size_t size)
{
    if (size > readableSize()) {
        throw std::out_of_range("not enough len");
    }

    size_t npos = m_pos % m_blockSize;
    size_t ncap = m_cur->size - npos;
    size_t bpos = 0;
    while (size > 0) {
        if (ncap >= size) {
            memcpy((char *)buf + bpos, m_cur->ptr + npos, size);
            if (m_cur->size == (npos + size)) {
                m_cur = m_cur->next;
            }
            m_pos += size;
            bpos += size;
            size = 0;
        } else {
            memcpy((char *)buf + bpos, m_cur->ptr + npos, ncap);
            m_pos += ncap;
            bpos += ncap;
            size -= ncap;
            m_cur = m_cur->next;
            ncap = m_cur->size;
            npos = 0;
        }
    }
}

void ByteArray::read(void *buf, size_t size, size_t position) const
{
    if (size > (m_size - position)) {
        throw std::out_of_range("not enough len");
    }

    size_t npos = position % m_blockSize;
    size_t ncap = m_cur->size - npos;
    size_t bpos = 0;
    Node *cur = m_cur;
    while (size > 0) {
        if (ncap >= size) {
            memcpy((char *)buf + bpos, cur->ptr + npos, size);
            if (cur->size == (npos + size)) {
                cur = cur->next;
            }
            position += size;
            bpos += size;
            size = 0;
        } else {
            memcpy((char *)buf + bpos, cur->ptr + npos, ncap);
            position += ncap;
            bpos += ncap;
            size -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
    }
}

void ByteArray::seek(size_t v)
{
    if (v > m_capacity) {
        throw std::out_of_range("set_position out of range");
    }
    m_pos = v;
    if (m_pos > m_size) {
        m_size = m_pos;
    }
    m_cur = m_root;
    while (v > m_cur->size) {
        v -= m_cur->size;
        m_cur = m_cur->next;
    }
    if (v == m_cur->size) {
        m_cur = m_cur->next;
    }
}

bool ByteArray::writeToFile(const std::string &name) const
{
    std::ofstream ofs;
    ofs.open(name, std::ios::trunc | std::ios::binary);
    if (!ofs) {
        LOG(core, ERROR) << "writeToFile name=" << name << " error , errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }

    int64_t read_size = readableSize();
    int64_t pos = m_pos;
    Node *cur = m_cur;

    while (read_size > 0) {
        int diff = pos % m_blockSize;
        int64_t len = (read_size > (int64_t)m_blockSize ? m_blockSize : read_size) - diff;
        ofs.write(cur->ptr + diff, len);
        cur = cur->next;
        pos += len;
        read_size -= len;
    }

    return true;
}

bool ByteArray::readFromFile(const std::string &name)
{
    std::ifstream ifs;
    ifs.open(name, std::ios::binary);
    if (!ifs) {
        LOG(core, ERROR) << "readFromFile name=" << name
                         << " error, errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }

    std::unique_ptr<char[]> buff(new char[m_blockSize]);
    while (!ifs.eof()) {
        ifs.read(buff.get(), m_blockSize);
        write(buff.get(), ifs.gcount());
    }
    return true;
}

void ByteArray::enlarge(size_t size)
{
    if (size == 0) {
        return;
    }
    size_t old_cap = capacity();
    if (old_cap >= size) {
        return;
    }

    size = size - old_cap;
    size_t count = std::ceil(1.0 * size / m_blockSize);
    Node *tmp = m_root;
    while (tmp->next) {
        tmp = tmp->next;
    }

    Node *first = NULL;
    for (size_t i = 0; i < count; ++i) {
        tmp->next = new Node(m_blockSize);
        if (first == NULL) {
            first = tmp->next;
        }
        tmp = tmp->next;
        m_capacity += m_blockSize;
    }

    if (old_cap == 0) {
        m_cur = first;
    }
}

std::string ByteArray::toString() const
{
    std::string str;
    str.resize(readableSize());
    if (str.empty()) {
        return str;
    }
    read(&str[0], str.size(), m_pos);
    return str;
}

std::string ByteArray::toHexString() const
{
    std::string str = toString();
    std::stringstream ss;

    for (size_t i = 0; i < str.size(); ++i) {
        if (i > 0 && i % 32 == 0) {
            ss << std::endl;
        }
        ss << std::setw(2) << std::setfill('0') << std::hex
           << (int)(uint8_t)str[i] << " ";
    }

    return ss.str();
}

uint64_t ByteArray::getReadBuffers(std::vector<iovec> &buffers, uint64_t len) const
{
    len = len > readableSize() ? readableSize() : len;
    if (len == 0) {
        return 0;
    }

    uint64_t size = len;

    size_t npos = m_pos % m_blockSize;
    size_t ncap = m_cur->size - npos;
    struct iovec iov;
    Node *cur = m_cur;

    while (len > 0) {
        if (ncap >= len) {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } else {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

uint64_t ByteArray::getReadBuffers(std::vector<iovec> &buffers, uint64_t len, uint64_t position) const
{
    len = len > readableSize() ? readableSize() : len;
    if (len == 0) {
        return 0;
    }

    uint64_t size = len;

    size_t npos = position % m_blockSize;
    size_t count = position / m_blockSize;
    Node *cur = m_root;
    while (count > 0) {
        cur = cur->next;
        --count;
    }

    size_t ncap = cur->size - npos;
    struct iovec iov;
    while (len > 0) {
        if (ncap >= len) {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } else {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;
            len -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

uint64_t ByteArray::getWriteBuffers(std::vector<iovec> &buffers, uint64_t len)
{
    if (len == 0) {
        return 0;
    }
    enlarge(len);
    uint64_t size = len;

    size_t npos = m_pos % m_blockSize;
    size_t ncap = m_cur->size - npos;
    struct iovec iov;
    Node *cur = m_cur;
    while (len > 0) {
        if (ncap >= len) {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = len;
            len = 0;
        } else {
            iov.iov_base = cur->ptr + npos;
            iov.iov_len = ncap;

            len -= ncap;
            cur = cur->next;
            ncap = cur->size;
            npos = 0;
        }
        buffers.push_back(iov);
    }
    return size;
}

} // namespace namespace meha::utils
