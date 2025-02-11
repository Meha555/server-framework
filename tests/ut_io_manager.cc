#include <arpa/inet.h>
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
        iom->start();
    }
    void TearDown() override
    {
        iom->stop();
        delete iom;
    }

    IOManager *iom;
};

// 模拟服务器
TEST_F(IOManagerTest, SocketIO)
{
    // system("nc -lvp 8800 &"); // 简单起见，使用netcat开启一个回声TCP服务器 FIXME nc貌似不行，用自己写的回声服务器就可以
    int sockfd;
    struct sockaddr_in server_addr;
    EXPECT_NE((sockfd = socket(AF_INET, SOCK_STREAM, 0)), -1);

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8800);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    EXPECT_NE(connect(sockfd, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr)), -1);

    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    LOG_INFO(root, "connect to echo server success");
    EXPECT_TRUE(iom->subscribeEvent(sockfd, FdContext::FdEvent::Write, [&]() {
        const char msg[] = "你好，我是客户端";
        LOG_FMT_INFO(root, "写就绪，通知服务端: %s", msg);
        EXPECT_EQ(send(sockfd, msg, sizeof(msg), 0), sizeof(msg));
    }));
    EXPECT_TRUE(iom->subscribeEvent(sockfd, FdContext::FdEvent::Read, [&]() {
        char buffer[1024];
        ssize_t nbytes = recv(sockfd, buffer, sizeof(buffer), 0);
        buffer[nbytes] = '\0';
        EXPECT_EQ(nbytes, sizeof("你好，我是客户端"));
        LOG_FMT_INFO(root, "读就绪，服务端回应: %s", buffer);
        EXPECT_STREQ(buffer, "你好，我是客户端");
        EXPECT_TRUE(iom->triggerAllEvents(sockfd));
        close(sockfd);
    }));
}

TEST_F(IOManagerTest, Timer)
{
    LOG(root, WARN) << "添加3s后的单发定时器";
    iom->addTimer(
        3000, []() {
            LOG_INFO(root, "sleep(3000)");
        },
        false);
    LOG(root, WARN) << "添加1s后的周期定时器";
    iom->addTimer(
        1000, []() {
            LOG_INFO(root, "sleep(1000)");
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