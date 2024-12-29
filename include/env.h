#include <string_view>
#include <unordered_map>
#include "mutex.hpp"
#include "singleton.h"

namespace meha::utils {

// @brief 环境变量管理类
class Env
{
public:
    explicit Env();

    std::string_view get(const std::string_view& key, const std::string_view& default_value = "");
    bool set(const std::string_view& key, const std::string_view& value);


private:
    mutable Mutex m_mutex;
    std::unordered_map<std::string_view, std::string_view> m_envs; // 环境变量：key=value
    const std::string_view m_cmd;
    const std::string_view m_cwd;
};

using EnvManager = Singleton<Env>;

} // namespace meha::utils