#pragma once

namespace meha {

/**
 * @brief 禁用拷贝构造操作
 * 继承使用
 */
class noncopyable {
public:
    noncopyable(const noncopyable &) = delete;
    noncopyable &operator=(const noncopyable &) = delete;

protected: // 设置为protected是说明这个类仅用于做虚基类，不能用于创建对象
    noncopyable() = default;
    ~noncopyable() = default;
};

// 禁用拷贝
#define DISABLE_COPY(Class)                                                                                            \
    Class(const Class &) = delete;                                                                                     \
    Class &operator=(const Class &) = delete;

// 禁用移动
#define DISABLE_MOVE(Class)                                                                                            \
    Class(const Class &&) noexcept = delete;                                                                           \
    Class &operator=(const Class &&) noexcept = delete;

#define DISABLE_COPY_MOVE(Class)                                                                                       \
    DISABLE_COPY(Class)                                                                                                \
    DISABLE_MOVE(Class)

}  // namespace meha
