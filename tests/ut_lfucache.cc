#include <gtest/gtest.h>
#include "extra/cache/lfucache.h"

using namespace meha;

class LFUCacheTest : public ::testing::Test {
protected:
    LFUCacheTest() : cache(3) {}

    LFUCache<int, std::string> cache;
};

/* -------------------------------- put方法测试用例 ------------------------------- */

TEST_F(LFUCacheTest, PutUpdateExistingKey)
{
    cache.put(1, "one");
    cache.put(1, "ONE");
    EXPECT_EQ(cache.get(1), "ONE");
}

TEST_F(LFUCacheTest, PutNewKeyWhenCacheNotFull) {
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    EXPECT_EQ(cache.get(1), "one");
    EXPECT_EQ(cache.get(2), "two");
    EXPECT_EQ(cache.get(3), "three");
    EXPECT_TRUE(cache.isFull());
}

TEST_F(LFUCacheTest, PutNewKeyWhenCacheIsFull) {
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    cache.get(1); // 1 is the most frequently accessed
    cache.put(4, "four"); // should evict 2
    EXPECT_EQ(cache.get(4), "four");
    EXPECT_EQ(cache.get(1), "one"); // 1 should still exist
    EXPECT_FALSE(cache.get(2)); // 2 是最久未访问的，会被淘汰

    cache.get(3);
    cache.put(5, "five"); // should evict 4
    EXPECT_FALSE(cache.get(4));
}

/* -------------------------------- get方法测试用例 ------------------------------- */

TEST_F(LFUCacheTest, GetExistingKey) {
    cache.put(1, "one");
    EXPECT_EQ(cache.get(1), "one");
}

TEST_F(LFUCacheTest, GetNonExistingKey) {
    EXPECT_FALSE(cache.get(1));
}

TEST_F(LFUCacheTest, TouchIncreasesFrequency)
{
    cache.put(1, "one");
    cache.put(2, "two");
    cache.get(1); // Increase frequency of key 1
    cache.put(3, "three");
    cache.put(4, "four"); // 2 should be evicted
    EXPECT_EQ(cache.get(2), std::nullopt);
    EXPECT_EQ(cache.get(1), "one");
    EXPECT_EQ(cache.get(3), "three");
    EXPECT_EQ(cache.get(4), "four");
}

/* ------------------------------- drop方法和purge方法测试用例 ------------------------------- */

TEST_F(LFUCacheTest, DropExistingKey) {
    cache.put(1, "one");
    cache.drop(1);
    EXPECT_EQ(cache.get(1), std::nullopt);
}

TEST_F(LFUCacheTest, PurgeExistingKey) {
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    cache.purge();
    EXPECT_FALSE(cache.isFull());
    EXPECT_FALSE(cache.get(1));
    EXPECT_FALSE(cache.get(2));
    EXPECT_FALSE(cache.get(3));
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}