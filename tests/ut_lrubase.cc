#include <gtest/gtest.h>
#include "extra/cache/lrucache.h"
#include "extra/cache/lrukcache.h"
#include "extra/cache/hashlrucache.h"

using namespace meha;

template<typename T>
CachePolicy<int, std::string>* CreateCache(size_t capacity);

// NOTE 函数模板不允许部分偏特化的实现（估计是部分偏特化也不是完全的实现，所以塞不了函数定义，而类本身是声明），只能全特化
template<>
CachePolicy<int, std::string>* CreateCache<LRUCache<int, std::string>>(size_t capacity)
{
    return new LRUCache<int, std::string>(capacity);
}

template<>
CachePolicy<int, std::string>* CreateCache<LRUKCache<int, std::string>>(size_t capacity)
{
    return new LRUKCache<int, std::string>(capacity);
}

template<>
CachePolicy<int, std::string>* CreateCache<HashLRUCache<int, std::string>>(size_t capacity)
{
    return new HashLRUCache<int, std::string>(capacity, 2);
}

template<typename T>
class LRUCacheBaseTest : public ::testing::Test {
protected:
    LRUCacheBaseTest () : cache(CreateCache<T>(4)) {}
    ~LRUCacheBaseTest() override {
        delete cache;
    }

    CachePolicy<int, std::string>* cache;
};

using Implemetations = ::testing::Types<LRUCache<int, std::string>, HashLRUCache<int, std::string>>;//LRUKCache<int, std::string>
TYPED_TEST_SUITE(LRUCacheBaseTest, Implemetations);

/* -------------------------------- put方法测试用例 ------------------------------- */

TYPED_TEST(LRUCacheBaseTest, PutUpdateExistingKey) {
    this->cache->put(1, "one");
    this->cache->put(1, "ONE");
    EXPECT_EQ(this->cache->get(1), "ONE");
}

TYPED_TEST(LRUCacheBaseTest, PutNewKeyWhenCacheNotFull) {
    this->cache->put(1, "one");
    this->cache->put(2, "two");
    this->cache->put(3, "three");
    this->cache->put(4, "four");
    EXPECT_EQ(this->cache->get(1), "one");
    EXPECT_EQ(this->cache->get(2), "two");
    EXPECT_EQ(this->cache->get(3), "three");
    EXPECT_EQ(this->cache->get(4), "four");
    EXPECT_TRUE(this->cache->isFull());
}

TYPED_TEST(LRUCacheBaseTest, PutNewKeyWhenCacheIsFull) {
    if (dynamic_cast<HashLRUCache<int, std::string>*>(this->cache)) {
        std::cout << "HashLRUCache不支持这个用例，因为不知道是哪个切片淘汰了数据" << std::endl;
        GTEST_SKIP();
    }
    this->cache->put(1, "one");
    this->cache->put(2, "two");
    this->cache->put(3, "three");
    this->cache->put(4, "four");
    this->cache->get(1); // 1 是最近访问的
    this->cache->put(5, "five");
    EXPECT_EQ(this->cache->get(5), "five");
    EXPECT_EQ(this->cache->get(1), "one"); // 1 should still exist
    EXPECT_EQ(this->cache->get(2), std::nullopt); // 2 是最久未访问的，会被淘汰
}

/* -------------------------------- get方法测试用例 ------------------------------- */

TYPED_TEST(LRUCacheBaseTest, GetExistingKey) {
    this->cache->put(1, "one");
    EXPECT_EQ(this->cache->get(1), "one");
}

TYPED_TEST(LRUCacheBaseTest, GetNonExistingKey) {
    EXPECT_EQ(this->cache->get(1), std::nullopt);
}

TYPED_TEST(LRUCacheBaseTest, TouchUpdatesOrder) {
    if (dynamic_cast<HashLRUCache<int, std::string>*>(this->cache)) {
        std::cout << "HashLRUCache不支持这个用例，因为不知道是哪个切片淘汰了数据" << std::endl;
        GTEST_SKIP();
    }
    this->cache->put(1, "one");
    this->cache->put(2, "two");
    this->cache->put(3, "three");
    this->cache->put(4, "four");
    this->cache->get(1); // 访问1，使得1变为最近访问的，最久未访问的变为2
    this->cache->put(5, "five"); // 此时应该淘汰最久未访问的2
    EXPECT_EQ(this->cache->get(5), "five");
    EXPECT_EQ(this->cache->get(1), "one"); // 1 should still exist
    EXPECT_EQ(this->cache->get(2), std::nullopt); // 2 should be evicted
}

/* ------------------------------- drop方法和purge方法测试用例 ------------------------------- */

TYPED_TEST(LRUCacheBaseTest, DropExistingKey) {
    this->cache->put(1, "one");
    this->cache->drop(1);
    EXPECT_EQ(this->cache->get(1), std::nullopt);
}
TYPED_TEST(LRUCacheBaseTest, PurgeExistingKey) {
    this->cache->put(1, "one");
    this->cache->put(2, "two");
    this->cache->put(3, "three");
    this->cache->put(4, "four");
    this->cache->purge();
    EXPECT_FALSE(this->cache->isFull());
    EXPECT_FALSE(this->cache->get(1));
    EXPECT_FALSE(this->cache->get(2));
    EXPECT_FALSE(this->cache->get(3));
    EXPECT_FALSE(this->cache->get(4));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}