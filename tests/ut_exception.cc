#include <gtest/gtest.h>
#include <iostream>
#include <unistd.h>

#include "utils/exception.h"
#include "application.h"

using namespace meha;

#define TEST_CASE ExceptionTest

void fn(int count)
{
    if (count <= 0) {
        throw Exception("Exception: fn 递归结束");
    }
    fn(count - 1);
}

void throw_system_error()
{
    if (write(0xffff, nullptr, 0) == -1) {
        throw SystemError("SystemError: 写入无法访问的地址");
    }
}

TEST(TEST_CASE, printBackTrace)
{
    try {
        fn(10);
    } catch (const Exception &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << e.stackTrace() << std::endl;
    }

    try {
        throw_system_error();
    } catch (const SystemError &e) {
        std::cerr << errno << std::endl;
        std::cerr << e.what() << std::endl;
        std::cerr << e.stackTrace() << std::endl;
    }
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