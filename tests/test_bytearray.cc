#include <algorithm>
#include <gtest/gtest.h>
#include <random>

#include "application.h"
#include "module/log.h"
#include "utils/bytearray.h"

using namespace meha;

#define TEST_CASE ByteArrayTest

TEST(TEST_CASE, MemorySerialization)
{
/*
 * 测试用例设计：
 * 随机生成长度为len，类型为type的数组，调用write_fun将这个数组全部写入块大小为block_size的ByteArray对象中，
 * 再将ByteArray的当前操作位置重设为0，也就是从起点开始，用read_fun重复读取数据，并与写入的数据比较，
 * 当读取出的数据与写入的数据全部相等时，该测试用例通过
 */
#define XX(type, len, write_fun, read_fun, block_size)  \
    {                                                   \
        std::vector<type> vec;                          \
        for (int i = 0; i < len; ++i) {                 \
            vec.push_back(std::rand());                 \
        }                                               \
        utils::ByteArray ba(block_size);                \
        for (auto &i : vec) {                           \
            ba.write_fun(i);                            \
        }                                               \
        ba.seek(0);                                     \
        for (size_t i = 0; i < vec.size(); ++i) {       \
            type v = ba.read_fun();                     \
            EXPECT_EQ(v, vec[i]);                       \
        }                                               \
        EXPECT_EQ(ba.readableSize(), 0);                \
        LOG(root, INFO) << #write_fun "/" #read_fun     \
                                      " (" #type ") "   \
                        << " len=" << len               \
                        << " block_size=" << block_size \
                        << " size=" << ba.size();       \
    }
    XX(int8_t, 100, writeFixedInt<8>, readFixedInt<8>, 1);
    XX(uint8_t, 100, writeFixedUint<8>, readFixedUint<8>, 1);
    XX(int16_t, 100, writeFixedInt<16>, readFixedInt<16>, 1);
    XX(uint16_t, 100, writeFixedUint<16>, readFixedUint<16>, 1);
    XX(int32_t, 100, writeFixedInt<32>, readFixedInt<32>, 1);
    XX(uint32_t, 100, writeFixedUint<32>, readFixedUint<32>, 1);
    XX(int64_t, 100, writeFixedInt<64>, readFixedInt<64>, 1);
    XX(uint64_t, 100, writeFixedUint<64>, readFixedUint<64>, 1);

    XX(int32_t, 100, writeVarintInt<32>, readVarintInt<32>, 1);
    XX(uint32_t, 100, writeVarintUint<32>, readVarintUint<32>, 1);
    XX(int64_t, 100, writeVarintInt<64>, readVarintInt<64>, 1);
    XX(uint64_t, 100, writeVarintUint<32>, readVarintUint<64>, 1);
#undef XX
}

TEST(TEST_CASE, FileSerialization)
{
/*
 * 测试用例设计：
 * 在前面的测试用例基础上，增加文件序列化和反序列化操作，
 * 当写入文件的内容与从文件读取出的内容完全一致时，测试用例通过
 */
#define XX(type, len, write_fun, read_fun, block_size)                               \
    {                                                                                \
        std::vector<type> vec;                                                       \
        for (int i = 0; i < len; ++i) {                                              \
            vec.push_back(std::rand());                                              \
        }                                                                            \
        utils::ByteArray ba(block_size);                                             \
        for (auto &i : vec) {                                                        \
            ba.write_fun(i);                                                         \
        }                                                                            \
        ba.seek(0);                                                                  \
        for (size_t i = 0; i < vec.size(); ++i) {                                    \
            type v = ba.read_fun();                                                  \
            EXPECT_EQ(v, vec[i]);                                                    \
        }                                                                            \
        EXPECT_EQ(ba.readableSize(), 0);                                             \
        LOG(root, INFO) << #write_fun "/" #read_fun                                  \
                                      " (" #type ") "                                \
                        << " len=" << len                                            \
                        << " block_size=" << block_size                              \
                        << " size=" << ba.size();                                    \
        ba.seek(0);                                                                  \
        EXPECT_TRUE(ba.writeToFile("/tmp/" #type "_" #len "-" #read_fun ".dat"));    \
        utils::ByteArray::sptr ba2(new utils::ByteArray(block_size * 2));            \
        EXPECT_TRUE(ba2->readFromFile("/tmp/" #type "_" #len "-" #read_fun ".dat")); \
        ba2->seek(0);                                                                \
        EXPECT_EQ(ba.toString(), ba2->toString());                                   \
        EXPECT_EQ(ba.pos(), 0);                                                      \
        EXPECT_EQ(ba2->pos(), 0);                                                    \
    }
    XX(int8_t, 100, writeFixedInt<8>, readFixedUint<8>, 1);
    XX(uint8_t, 100, writeFixedUint<8>, readFixedUint<8>, 1);
    XX(int16_t, 100, writeFixedInt<16>, readFixedUint<16>, 1);
    XX(uint16_t, 100, writeFixedUint<16>, readFixedUint<16>, 1);
    XX(int32_t, 100, writeFixedInt<32>, readFixedUint<32>, 1);
    XX(uint32_t, 100, writeFixedUint<32>, readFixedUint<32>, 1);
    XX(int64_t, 100, writeFixedInt<64>, readFixedUint<64>, 1);
    XX(uint64_t, 100, writeFixedUint<64>, readFixedUint<64>, 1);

    XX(int32_t, 100, writeVarintInt<32>, readVarintInt<32>, 1);
    XX(uint32_t, 100, writeVarintUint<32>, readVarintUint<32>, 1);
    XX(int64_t, 100, writeVarintInt<64>, readVarintInt<64>, 1);
    XX(uint64_t, 100, writeVarintUint<32>, readVarintUint<64>, 1);
#undef XX
}

TEST(TEST_CASE, StringSerialization)
{
/*
 * 测试用例设计：
 * 在前面的测试基础上，增加对字符串序列化/反序列化的测试
 */
#define XX(len, write_fun, read_fun, block_size)        \
    {                                                   \
        std::string s = "qwertyuiopasdfghjklzxcvbnm";   \
        std::vector<std::string> vec;                   \
        std::random_device rd;                          \
        std::default_random_engine rg(rd());            \
        for (int i = 0; i < len; i++) {                 \
            std::shuffle(s.begin(), s.end(), rg);       \
            vec.push_back(s);                           \
        }                                               \
        utils::ByteArray ba(block_size);                \
        for (auto &i : vec) {                           \
            ba.write_fun(i);                            \
        }                                               \
        ba.seek(0);                                     \
        for (size_t i = 0; i < vec.size(); ++i) {       \
            std::string v = ba.read_fun();              \
            EXPECT_EQ(v, vec[i]);                       \
        }                                               \
        EXPECT_EQ(ba.readableSize(), 0);                \
        LOG(root, INFO) << #write_fun "/" #read_fun     \
                                      " (string) "      \
                        << " len=" << len               \
                        << " block_size=" << block_size \
                        << " size=" << ba.size();       \
    }
    XX(100, writeFixedString<16>, readFixedString<16>, 10);
    XX(100, writeFixedString<32>, readFixedString<32>, 10);
    XX(100, writeFixedString<64>, readFixedString<64>, 10);
    XX(100, writeStringVarintInt, readStringVarintInt, 26);
#undef XX
}

int main(int argc, char *argv[])
{
    Application app;
    return app.boot(BootArgs{
        .argc = argc,
        .argv = argv,
        .configFile = "/home/will/Workspace/Devs/projects/server-framework/misc/config.yml",
        .mainFunc = [](int argc, char **argv) -> int {
            ::testing::InitGoogleTest(&argc, argv);
            return RUN_ALL_TESTS();
        }});
}