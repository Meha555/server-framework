#include "exception.h"
#include <gtest/gtest.h>
#include <iostream>
#include <unistd.h>

#define TEST_CASE ExceptionTest

void fn(int count)
{
    if (count <= 0) {
        throw meha::Exception("Exception: fn 递归结束");
    }
    fn(count - 1);
}

void throw_system_error()
{
    if (write(0xffff, nullptr, 0) == -1) {
        throw meha::SystemError("SystemError: 写入无法访问的地址");
    }
}

TEST(TEST_CASE, 测试打印调用栈)
{
    try {
        fn(10);
    } catch (const meha::Exception &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << e.stackTrace() << std::endl;
    }

    try {
        throw_system_error();
    } catch (const meha::SystemError &e) {
        std::cerr << errno << std::endl;
        std::cerr << e.what() << std::endl;
        std::cerr << e.stackTrace() << std::endl;
    }
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
