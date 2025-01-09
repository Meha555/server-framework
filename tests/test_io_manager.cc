#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <gtest/gtest.h>

#include "application.h"
#include "io_manager.h"
#include "module/log.h"

using namespace meha;

class IOManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        iom = new IOManager(2);
    }
    void TearDown() override
    {
        delete iom;
    }

    IOManager *iom;
};

// 模拟服务器
TEST_F(IOManagerTest, SocketIO)
{
    system("nc -lvp 8800 &"); // 简单起见，使用netcat开启一个回声TCP服务器
    int sockfd;
    struct sockaddr_in server_addr
    {
    };
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8800);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (connect(sockfd, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr)) == -1) {
        perror("connect");
        exit(1);
    }

    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    LOG_INFO(root, "connect to echo server success");
    iom->subscribeEvent(sockfd, FdContext::FdEvent::Write, [&]() {
        const char msg[] = "你好，我是客户端";
        LOG_FMT_INFO(root, "写就绪，通知服务端: %s", msg);
        send(sockfd, msg, sizeof(msg), 0);
    });
    iom->subscribeEvent(sockfd, FdContext::FdEvent::Read, [&]() {
        char buffer[1024];
        ssize_t nbytes = recv(sockfd, buffer, sizeof(buffer), 0);
        buffer[nbytes] = '\0';
        LOG_FMT_INFO(root, "读就绪，服务端回应: %s", buffer);
        iom->triggerAllEvents(sockfd);
        close(sockfd);
    });
}

TEST_F(IOManagerTest, Timer)
{
    iom->addTimer(
        1000, []() {
            LOG_INFO(root, "sleep(1000)");
        },
        true);
    iom->addTimer(
        500, []() {
            LOG_INFO(root, "sleep(500)");
        },
        true);
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