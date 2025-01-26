#include <gtest/gtest.h>
#include "extra/cache/lrucache.h"

using namespace meha;

class LRUCacheTest : public ::testing::Test {
protected:
    LRUCacheTest () : cache(3) {}

    LRUCache<int, std::string> cache;
};

/* -------------------------------- put方法测试用例 ------------------------------- */

TEST_F(LRUCacheTest, PutUpdateExistingKey) {
    cache.put(1, "one");
    cache.put(1, "ONE");
    EXPECT_EQ(cache.get(1), "ONE");
}

TEST_F(LRUCacheTest, PutNewKeyWhenCacheNotFull) {
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    EXPECT_EQ(cache.get(1), "one");
    EXPECT_EQ(cache.get(2), "two");
    EXPECT_EQ(cache.get(3), "three");
    EXPECT_TRUE(cache.isFull());
}

TEST_F(LRUCacheTest, PutNewKeyWhenCacheIsFull) {
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    cache.get(1); // 1 是最近访问的
    cache.put(4, "four");
    EXPECT_EQ(cache.get(4), "four");
    EXPECT_EQ(cache.get(1), "one"); // 1 should still exist
    EXPECT_EQ(cache.get(2), std::nullopt); // 2 是最久未访问的，会被淘汰
}

/* -------------------------------- get方法测试用例 ------------------------------- */

TEST_F(LRUCacheTest, GetExistingKey) {
    cache.put(1, "one");
    EXPECT_EQ(cache.get(1), "one");
}

TEST_F(LRUCacheTest, GetNonExistingKey) {
    EXPECT_EQ(cache.get(1), std::nullopt);
}

TEST_F(LRUCacheTest, TouchUpdatesOrder) {
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    cache.get(1); // 访问1，使得1变为最近访问的，最久未访问的变为2
    cache.put(4, "four"); // 此时应该淘汰最久未访问的2
    EXPECT_EQ(cache.get(4), "four");
    EXPECT_EQ(cache.get(1), "one"); // 1 should still exist
    EXPECT_EQ(cache.get(2), std::nullopt); // 2 should be evicted
}

/* ------------------------------- drop方法和purge方法测试用例 ------------------------------- */

TEST_F(LRUCacheTest, DropExistingKey) {
    cache.put(1, "one");
    cache.drop(1);
    EXPECT_EQ(cache.get(1), std::nullopt);
}

TEST_F(LRUCacheTest, PurgeExistingKey) {
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    cache.purge();
    EXPECT_FALSE(cache.isFull());
    EXPECT_FALSE(cache.get(1));
    EXPECT_FALSE(cache.get(2));
    EXPECT_FALSE(cache.get(3));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}