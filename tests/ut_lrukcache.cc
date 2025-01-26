#include <gtest/gtest.h>
#include "extra/cache/lrukcache.h"

using namespace meha;

class LRUKCacheTest : public ::testing::Test {
protected:
    LRUKCacheTest() : cache(3, 4, 2) {}

    LRUKCache<int, std::string> cache;
};

/* -------------------------------- put方法测试用例 ------------------------------- */

TEST_F(LRUKCacheTest, PutUpdateExistingKey)
{
    cache.put(1, "one");
    cache.put(1, "ONE");
    EXPECT_EQ(cache.get(1), "ONE");
}

TEST_F(LRUKCacheTest, PutNewKeyWhenCacheNotFull)
{
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    EXPECT_EQ(cache.get(1), "one");
    EXPECT_EQ(cache.get(2), "two");
    EXPECT_EQ(cache.get(3), "three");
}

TEST_F(LRUKCacheTest, PutNewKeyWhenCacheIsFull)
{
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    
    cache.get(1); // 1 是最近访问的
    cache.get(4);
    cache.put(4, "four"); // 此时应该淘汰2
    EXPECT_EQ(cache.get(4), "four");
    EXPECT_EQ(cache.get(1), "one");
    EXPECT_EQ(cache.get(2), std::nullopt);
}

/* -------------------------------- get方法测试用例 ------------------------------- */

TEST_F(LRUKCacheTest, GetExistingKey)
{
    cache.put(1, "one");
    EXPECT_EQ(cache.get(1), "one");
}

TEST_F(LRUKCacheTest, GetNonExistingKey)
{
    EXPECT_EQ(cache.get(1), std::nullopt);
}

TEST_F(LRUKCacheTest, GetKeyAfterPutForKTimes)
{
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    cache.put(4, "four");
    EXPECT_EQ(cache.get(4), std::nullopt);
    cache.get(4);
    EXPECT_EQ(cache.get(4), std::nullopt);
    cache.put(4, "four");
    EXPECT_EQ(cache.get(4), "four");

    EXPECT_EQ(cache.get(1), std::nullopt);
    EXPECT_EQ(cache.get(2), "two");
    EXPECT_EQ(cache.get(3), "three");
}

TEST_F(LRUKCacheTest, TouchUpdatesOrder) {
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    cache.get(1); // 访问1，使得1变为最近访问的，最久未访问的变为2

    cache.put(4, "four");
    cache.put(4, "four"); // 此时应该淘汰最久未访问的2

    EXPECT_EQ(cache.get(4), "four");
    EXPECT_EQ(cache.get(1), "one"); // 1 should still exist
    EXPECT_EQ(cache.get(2), std::nullopt); // 2 should be evicted
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}