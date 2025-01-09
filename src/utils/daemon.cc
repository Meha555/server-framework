#include <cstring>
#include <ctime>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "daemon.h"
#include "module/log.h"

namespace meha::utils
{

static meha::ConfigItem<uint32_t>::sptr g_daemon_restart_interval = meha::Config::Lookup("daemon.restart_interval", (uint32_t)5, "daemon restart interval");

std::string ProcessInfo::toString() const
{
    std::stringstream ss;
    ss << "[ProcessInfo parent_id=" << parent_id
       << " main_id=" << main_id
       << " parent_start_time=" << Time2Str(parent_start_time)
       << " main_start_time=" << Time2Str(main_start_time)
       << " restart_count=" << restart_count << "]";
    return ss.str();
}

static int real_start(int argc, char **argv, std::function<int(int argc, char **argv)> mainfun)
{
    return mainfun(argc, argv);
}

static int real_daemon(int argc, char **argv, std::function<int(int argc, char **argv)> mainfun)
{
    daemon(1, 0);
    ProcessInfoMgr::Instance()->parent_id = getpid();
    ProcessInfoMgr::Instance()->parent_start_time = time(0);
    while (true) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程返回
            ProcessInfoMgr::Instance()->main_id = getpid();
            ProcessInfoMgr::Instance()->main_start_time = time(0);
            LOG(root, INFO) << "process start pid=" << getpid();
            return real_start(argc, argv, mainfun);
        } else if (pid < 0) {
            LOG(root, ERROR) << "fork fail return=" << pid << " errno=" << errno << " errstr=" << strerror(errno);
            return -1;
        } else {
            // 父进程返回
            int status = 0;
            waitpid(pid, &status, 0);
            if (status) {
                LOG(root, ERROR) << "child crash pid=" << pid << " status=" << status;
            } else {
                LOG(root, INFO) << "child finished pid=" << pid;
                break;
            }
            ProcessInfoMgr::Instance()->restart_count += 1;
            sleep(g_daemon_restart_interval->getValue());
        }
    }
    return 0;
}

int StartDaemon(int argc, char **argv, std::function<int(int argc, char **argv)> main_cb, bool is_daemon)
{
    if (!is_daemon) {
        return real_start(argc, argv, main_cb);
    }
    return real_daemon(argc, argv, main_cb);
}

}
