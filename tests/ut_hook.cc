#include <arpa/inet.h>
#include <gtest/gtest.h>

#include "application.h"
#include "io_manager.h"
#include "module/log.h"
#include "utils/utils.h"

using namespace meha;

class HookTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        iom = new IOManager(1);
        iom->start();
    }
    void TearDown() override
    {
        iom->stop();
        delete iom;
    }

    IOManager *iom;
};

TEST_F(HookTest, HookSleep)
{
    LOG_FMT_INFO(root, "main() 开始 in fiber[%ld]", utils::GetFiberID());
    for (int i = 0; i < 3; i++) {
        iom->schedule([]() {
            LOG_FMT_INFO(root, "sleep(1) 开始 in fiber[%ld]", utils::GetFiberID());
            sleep(1);
            LOG_FMT_INFO(root, "sleep(1) 结束 in fiber[%ld]", utils::GetFiberID());
        });
        iom->schedule([]() {
            LOG_FMT_INFO(root, "sleep(3) 开始 in fiber[%ld]", utils::GetFiberID());
            sleep(3);
            LOG_FMT_INFO(root, "sleep(3) 结束 in fiber[%ld]", utils::GetFiberID());
        });
    }
    LOG_FMT_INFO(root, "main() 结束 in fiber[%ld]", utils::GetFiberID());
}

TEST_F(HookTest, HookSocket)
{
    LOG_FMT_INFO(root, "main() 开始 in fiber[%ld]", utils::GetFiberID());
    iom->schedule([]() {
        int sockfd;
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("socket");
            exit(1);
        }
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));

        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);

        LOG_FMT_INFO(root, "开始连接 in fiber[%ld]", utils::GetFiberID());
        if (connect(sockfd, (struct sockaddr *)(&addr), sizeof(struct sockaddr)) == -1) {
            perror("connect");
            exit(1);
        }
        LOG_FMT_INFO(root, "连接成功 in fiber[%ld]", utils::GetFiberID());

        const char data[] = "GET / HTTP/1.0\r\n\r\n";
        ssize_t nbytes = send(sockfd, data, sizeof(data), 0);
        LOG_FMT_INFO(root, "发送出 nbytes=%ld, errno=%d (%s)", nbytes, errno, strerror(errno));
        if (nbytes <= 0) {
            return;
        }

        char buff[4096]; // 注意不要超出协程栈了
        memset(buff, 0, sizeof(buff));
        nbytes = recv(sockfd, buff, sizeof(buff), 0);
        LOG_FMT_INFO(root, "接收到 nbytes=%ld, errno=%d (%s)", nbytes, errno, strerror(errno));
        if (nbytes <= 0) {
            return;
        }
        LOG_FMT_INFO(root, "接收到的内容: %s", buff);
    });
    LOG_FMT_INFO(root, "main() 结束 in fiber[%ld]", utils::GetFiberID());
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