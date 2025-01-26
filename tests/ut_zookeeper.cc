#include <gtest/gtest.h>
#include "application.h"
#include "io_manager.h"
#include "module/log.h"
#include "utils/string.h"
#include "zookeeper/zk_client.h"

using namespace meha;

#define TEST_CASE ZooKeeperTest

static void on_watcher(int type, int stat, const std::string &path, ZKClient::sptr client)
{
    LOG(root, INFO) << " type=" << type
                    << " stat=" << stat
                    << " path=" << path
                    << " client=" << client
                    << " fiber=" << Fiber::GetCurrent()
                    << " iomanager=" << IOManager::GetCurrent();

    if (stat == ZOO_CONNECTED_STATE) {
        if (mApp->BootArgs().argc == 1) {
            std::vector<std::string> vals;
            Stat stat;
            int rt = client->getChildren("/", vals, true, &stat);
            if (rt == ZOK) {
                LOG(root, INFO) << "[" << utils::Join(vals.begin(), vals.end(), ",") << "]";
            } else {
                LOG(root, INFO) << "getChildren error " << rt;
            }
        } else {
            std::string new_val;
            new_val.resize(255);
            int rt = client->create("/testing", "", new_val, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL);
            if (rt == ZOK) {
                LOG(root, INFO) << "[" << new_val.c_str() << "]";
            } else {
                LOG(root, INFO) << "getChildren error " << rt;
            }

            rt = client->create("/testing", "", new_val, &ZOO_OPEN_ACL_UNSAFE, ZOO_SEQUENCE | ZOO_EPHEMERAL);
            if (rt == ZOK) {
                LOG(root, INFO) << "create [" << new_val.c_str() << "]";
            } else {
                LOG(root, INFO) << "create error " << rt;
            }

            rt = client->get("/hello", new_val, true);
            if (rt == ZOK) {
                LOG(root, INFO) << "get [" << new_val.c_str() << "]";
            } else {
                LOG(root, INFO) << "get error " << rt;
            }

            rt = client->create("/hello", "", new_val, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL);
            if (rt == ZOK) {
                LOG(root, INFO) << "get [" << new_val.c_str() << "]";
            } else {
                LOG(root, INFO) << "get error " << rt;
            }

            rt = client->set("/hello", "xxx");
            if (rt == ZOK) {
                LOG(root, INFO) << "set [" << new_val.c_str() << "]";
            } else {
                LOG(root, INFO) << "set error " << rt;
            }

            rt = client->remove("/hello");
            if (rt == ZOK) {
                LOG(root, INFO) << "remove [" << new_val.c_str() << "]";
            } else {
                LOG(root, INFO) << "remove error " << rt;
            }
        }
    } else if (stat == ZOO_EXPIRED_SESSION_STATE) {
        client->reconnect();
    }
}

TEST(TEST_CASE, Watcher)
{
    IOManager iom(1);
    ZKClient::sptr client(new ZKClient);
    if (mApp->BootArgs().argc > 1) {
        LOG(root, INFO) << client->init("127.0.0.1:2181", 3000, on_watcher);
        iom.addTimer(1115000, [client]() {
            client->close();
        });
    } else {
        LOG(root, INFO) << client->init("127.0.0.1:2181,127.0.0.1:2182,127.0.0.1:2181", 3000, on_watcher);
        iom.addTimer(5000, []() {}, true);
    }
    iom.stop();
}

int main(int argc, char **argv)
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
    return 0;
}
