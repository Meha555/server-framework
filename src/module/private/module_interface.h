#pragma once

#include <memory>
#include <string>

namespace meha::internal
{

/**
 * @brief 框架内置模块
 * TODO @note 貌似要求在prestart阶段初始化，如果要编排顺序的话就只能用static对象初始化来搞
 */
class Module
{
public:
    using sptr = std::shared_ptr<Module>;
    enum InitTime {
        PreStart,
        OnStart
    };

    explicit Module(const std::string &name)
        : m_name(name)
        , m_initialized(false)
    {
    }
    virtual ~Module() = default;

    /**
     * @brief 模块初始化
     */
    void init()
    {
        if (m_initialized) {
            return;
        }
        m_initialized = initInternel();
    }

    /**
     * @brief 模块析构时清理其产生的影响
     * @note 应该用不上，因为这个Module是必须的
     */
    virtual void cleanup()
    {
        if (!m_initialized) {
            return;
        }
    }

    /**
     * @brief 子类必须实现此方法来规定模块初始化的优先级
     * @note 默认值为0，即优先级最高
     * @return uint32_t 优先级，范围[0, 100]
     * 数字越小，优先级越高，越先初始化；优先级相同时，谁后插入初始化队列，谁先初始化
     * 相同InitTime的优先级才有可比性
     */
    virtual uint32_t priority() const
    {
        return 0;
    }

    virtual InitTime initTime() const = 0;

    const std::string &name() const
    {
        return m_name;
    }

    bool initialized() const
    {
        return m_initialized;
    }

protected:
    virtual bool initInternel() = 0;

private:
    std::string m_name;
    bool m_initialized; // TODO 疑问：如果这个是static的，那么在子类中的也是共用的吗？
};

}