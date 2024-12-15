#pragma once

#include "noncopyable.h"
#include <memory>

namespace meha
{

/**
 * @brief 单例包装类
 * @note 被包装类型是否真的是单例全靠使用者自觉
 * @details 调用 Singleton::GetInstance 返回被包装类型的原生指针
 */
template<typename T>
class Singleton final
{
public:
    static T *GetInstance()
    {
        static T instance; // static 变量的初始化能保证线程安全
        return &instance;
    }

private:
    Singleton() = default;
};

/**
 * @brief 单例包装类（要求被包装类型有默认构造函数）
 * @note 被包装类型是否真的是单例全靠使用者自觉
 * @details Singleton::GetInstance 返回被包装类型的 std::shared_ptr 智能指针
 */
template<typename T>
class SingletonPtr final
{
public:
    static std::shared_ptr<T> GetInstance()
    {
        static auto instance = std::make_shared<T>(); // static 变量的初始化能保证线程安全
        return instance;
    }

private:
    SingletonPtr() = default;
};

} // namespace meha
