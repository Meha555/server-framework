#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "io_manager.h"
#include "module/log.h"

using namespace meha;

#define TEST_CASE IOManagerTest

void test_fiber()
{
    for (int i = 0; i < 3; i++) {
        LOG_INFO(root, "hello test");
        // meha::Fiber::YieldToHold(); //FIXME
    }
}

TEST(TEST_CASE, CreateIOManager)
{
    char buffer[1024];
    const char msg[] = "懂的都懂";
    meha::IOManager iom(2);
    // FIXME 这个接口太难用了
    iom.schedule(std::function<void()>(test_fiber));
    int sockfd;
    sockaddr_in server_addr{};
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("啊这");
        exit(1);
    }
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8800);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (connect(sockfd, (struct sockaddr *)(&server_addr),
                sizeof(struct sockaddr))
        == -1) {
        perror("啊这");
        exit(1);
    } else {
        fcntl(sockfd, F_SETFL, O_NONBLOCK);
        LOG_INFO(root, "开始了开始了");
        iom.subscribeEvent(sockfd, meha::FdContext::FdEvent::READ, [&]() {
            recv(sockfd, buffer, sizeof(buffer), 0);
            LOG_FMT_INFO(root, "服务端回应: %s", buffer);
            iom.triggerAllEvents(sockfd);
            close(sockfd);
        });
        iom.subscribeEvent(sockfd, meha::FdContext::FdEvent::WRITE, [&]() {
            memcpy(buffer, msg, sizeof(buffer));
            LOG_FMT_INFO(root, "告诉服务端: %s", buffer);
            send(sockfd, buffer, sizeof(buffer), 0);
        });
    }
}

TEST(TEST_CASE, Timer)
{
    meha::IOManager iom(2);
    // iom.schedule(test_fiber);
    iom.addTimer(
        1000, []() {
            LOG_INFO(root, "sleep(1000)");
        },
        true);
    iom.addTimer(
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