#pragma once

#include <map>

#include "config.h"

namespace meha::internal
{

/**
 * @brief 负责初始化各模块
 * @note 不再允许创建各模块中的全局对象（尤其是Logger）
 */
class ModuleIniter final
{
    using Self = ModuleIniter;

public:
    ModuleIniter(const std::string &configFile)
        : m_configFile(configFile)
    {
    }

    ~ModuleIniter()
    {
        for (const auto &[idx, mod] : m_modules) {
            mod->cleanup();
            std::cerr << "[module-initer] cleanup " << mod->name() << std::endl;
        }
    }

    void initialize()
    {
        for (auto &[idx, mod] : m_modules) {
            assert(!mod->initialized());
            mod->init();
            std::cerr << "[module-initer] init " << mod->name() << std::endl;
        }
        if (!s_loaded) {
            std::cerr << "[module-initer] load conf " << m_configFile << std::endl;
            Config::LoadFromFile(m_configFile);
        }
    }

    Self &addModule(Module::sptr mod)
    {
        if (!mod->initialized()) {
            m_modules.emplace(mod->priority(), mod);
        }
        return *this;
    }

    Self &delModule(const uint32_t priority)
    {
        const auto it = m_modules.find(priority);
        if (it != m_modules.end()) {
            m_modules.erase(it);
        }
        return *this;
    }

    Self &delModule(const std::string &name)
    {
        const auto it = std::find_if(m_modules.cbegin(), m_modules.cend(), [&name](const auto &pair) {
            return pair.second->name() == name;
        });
        if (it != m_modules.end()) {
            m_modules.erase(it);
        }
        return *this;
    }

public:
    static bool s_loaded;

private:
    const std::string m_configFile;
    std::map<uint32_t, Module::sptr> m_modules;
};

inline bool ModuleIniter::s_loaded = false;

}