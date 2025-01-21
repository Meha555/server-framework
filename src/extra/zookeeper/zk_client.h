#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef THREADED
#define THREADED
#endif

#include <zookeeper/zookeeper.h>

namespace meha
{

class ZKClient final : public std::enable_shared_from_this<ZKClient>
{
public:
    // 由于枚举值必须是constant-expression，而zookeeper.h中定义的是const，所以没法直接作为枚举，这里只能用结构体代替
    struct EventType
    {
        static const int CREATED; // = ZOO_CREATED_EVENT;
        static const int DELETED; // = ZOO_DELETED_EVENT;
        static const int CHANGED; // = ZOO_CHANGED_EVENT;
        static const int CHILD; // = ZOO_CHILD_EVENT;
        static const int SESSION; // = ZOO_SESSION_EVENT;
        static const int NOWATCHING; // = ZOO_NOTWATCHING_EVENT;
    };
    struct FlagsType
    {
        static const int PERSISTENT; // = ZOO_PERSISTENT;
        static const int EPHEMERAL; // = ZOO_EPHEMERAL;
        static const int PERSISTENT_SEQUENTIAL; // = ZOO_PERSISTENT_SEQUENTIAL;
        static const int EPHEMERAL_SEQUENTIAL; // = ZOO_EPHEMERAL_SEQUENTIAL;
        static const int CONTAINER; // = ZOO_CONTAINER;
        static const int PERSISTENT_WITH_TTL; // =ZOO_PERSISTENT_WITH_TTL;
        static const int PERSISTENT_SEQUENTIAL_WITH_TTL; // = ZOO_PERSISTENT_SEQUENTIAL_WITH_TTL;
        static const int SEQUENCE; //  = ZOO_SEQUENCE;
    };
    struct StateType
    {
        static const int EXPIRED_SESSION; // = ZOO_EXPIRED_SESSION_STATE;
        static const int AUTH_FAILED; // = ZOO_AUTH_FAILED_STATE;
        static const int CONNECTING; // = ZOO_CONNECTING_STATE;
        static const int ASSOCIATING; // = ZOO_ASSOCIATING_STATE;
        static const int CONNECTED; // = ZOO_CONNECTED_STATE;
        static const int READONLY; // = ZOO_READONLY_STATE;
        static const int NOTCONNECTED; // = ZOO_NOTCONNECTED_STATE;
    };

    using sptr = std::shared_ptr<ZKClient>;
    using WatcherCallback = std::function<void(int type, int stat, const std::string &path, ZKClient::sptr)>;
    using LogFuncPtr = void (*)(const char *message);

    explicit ZKClient();
    ~ZKClient();

    bool init(const std::string &hosts, int recv_timeout, WatcherCallback cb, LogFuncPtr lcb = nullptr);
    int32_t setServers(const std::string &hosts);

    int32_t create(const std::string &path, const std::string &val, std::string &new_path, const struct ACL_vector *acl = &ZOO_OPEN_ACL_UNSAFE, int flags = 0);
    int32_t exists(const std::string &path, bool watch, Stat *stat = nullptr);
    int32_t remove(const std::string &path, int version = -1);
    int32_t get(const std::string &path, std::string &val, bool watch, Stat *stat = nullptr);
    int32_t getConfig(std::string &val, bool watch, Stat *stat = nullptr);
    int32_t set(const std::string &path, const std::string &val, int version = -1, Stat *stat = nullptr);
    int32_t getChildren(const std::string &path, std::vector<std::string> &val, bool watch, Stat *stat = nullptr);
    int32_t close();
    int32_t state();
    std::string currentServer();

    bool reconnect();

private:
    static void OnWatcher(zhandle_t *zh, int type, int stat, const char *path, void *watcherCtx);
    using WatcherCallback2 = std::function<void(int type, int stat, const std::string &path)>;

private:
    zhandle_t *m_handle;
    std::string m_hosts;
    WatcherCallback2 m_watcherCb;
    LogFuncPtr m_logCb;
    int32_t m_recvTimeout;
};

}
