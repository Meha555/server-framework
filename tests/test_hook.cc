#include <arpa/inet.h>
#include <gtest/gtest.h>

#include "application.h"
#include "io_manager.h"
#include "module/log.h"

using namespace meha;

#define TEST_CASE HookTest

TEST(TEST_CASE, HookSleep)
{
    meha::IOManager iom(1);
    LOG_DEBUG(root, "main() 开始");
    for (int i = 0; i < 3; i++)
    {
        iom.schedule([]()
                     {
            LOG_DEBUG(root, "sleep(2) 开始");
            sleep(2);
            LOG_DEBUG(root, "sleep(2) 结束"); });
        iom.schedule([]()
                     {
            LOG_DEBUG(root, "sleep(3) 开始");
            sleep(3);
            LOG_DEBUG(root, "sleep(3) 结束"); });
    }

    LOG_DEBUG(root, "main() 结束");
}

TEST(TEST_CASE, HookSocket)
{
    std::function<void()> test_sock = [] () {
        int sockfd;
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        {
            perror("啊这");
            exit(1);
        }
        sockaddr_in addr{};
        memset(&addr, 0, sizeof(addr));

        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        inet_pton(AF_INET, "192.168.100.254", &addr.sin_addr.s_addr);

        LOG_INFO(root, "开始连接");
        if (connect(sockfd, (struct sockaddr*)(&addr), sizeof(struct sockaddr)) == -1)
        {
            perror("啊这");
            exit(1);
        }
        LOG_INFO(root, "连接成功");

        const char data[] = "GET / HTTP/1.0\r\n\r\n";
        auto rt = send(sockfd, data, sizeof(data), 0);
        LOG_FMT_DEBUG(root, "sent() rt=%ld, errno=%d", rt, errno);
        if (rt <= 0)
        {
            return;
        }

        std::string buff;
        buff.resize(4096);
        rt = recv(sockfd, &buff[0], buff.size(), 0);
        LOG_FMT_DEBUG(root, "recv() rt=%ld, errno=%d", rt, errno);
        if (rt <= 0)
        {
            return;
        }
        buff.resize(rt);
        LOG_FMT_DEBUG(root, "buff:\n %s", buff.c_str());
    };
    LOG_DEBUG(root, "main() 开始");
    meha::IOManager iom(1);
    iom.schedule(test_sock);
    LOG_DEBUG(root, "main() 结束");
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