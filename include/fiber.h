// #ifndef SERVER_FRAMEWORK_FIBER_H
// #define SERVER_FRAMEWORK_FIBER_H
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <ucontext.h>

#include "config.hpp"

namespace meha {

class Scheduler;

/**
 * @brief 协程类（不可拷贝但可移动）
 * @note 因为Fiber需要获取自身的智能指针，因此需要继承enable_shared_from_this。
 */
class Fiber : public std::enable_shared_from_this<Fiber>,
              public meha::noncopyable {
  friend class Scheduler;

public:
  using sptr = std::shared_ptr<Fiber>;
  using uptr = std::unique_ptr<Fiber>;
  using FiberFunc = std::function<void()>;

  // 协程状态，用于调度
  enum State // FIXME 协程状态
  {
    INIT,  // 初始化（尚未执行过）
    READY, // 就绪（准备好执行）
    EXEC,  // 执行（正在运行）
    TERM,  // 结束（运行结束）
  };

public:
  /**
   * @brief 创建新子协程
   * @param callback 协程执行函数
   * @param stack_size 协程栈大小，如果传 0，使用配置项 "fiber.stack_size"
   * 定义的值
   * */
  explicit Fiber(FiberFunc callback, size_t stack_size = 0);
  Fiber(Fiber &&rhs) noexcept;
  Fiber &operator=(Fiber &&rhs) noexcept;
  ~Fiber();

  /**
   * @brief 复用栈更换协程执行函数
   * @param callback 要更换为的执行函数
   * @note 此时协程的状态应该是 INIT 或 TERM 或 ERROR
   */
  void reset(FiberFunc &&callback) noexcept;

  /**
   * @brief 换入该协程执行
   * @details 该协程状态变为EXEC，正在运行的协程状态变为HOLD
   * @note 在主协程中调用该函数，因此正在运行的写成是主协程
   * @pre 在没有Scheduler下使用
   */
  void resume() noexcept;

  /**
   * @brief 挂起当前协程，切回主协程
   * @details 当前协程与上次
   * @note 在该协程中调用该函数
   * @pre 在没有Scheduler下使用
   */
  void yield() noexcept;

  // 获取该协程 id
  uint64_t getID() const { return m_id; }

  // 获取该协程状态
  State getState() const { return m_state; }

  // 判断协程是否执行结束
  bool isFinished() const noexcept;

private:
  // 无参构造用于创建 master fiber，设置为私有的即不允许用户创建master fiber
  explicit Fiber();

public:
  // TODO - 由于没有所谓“fiber_local”，因此以下函数应该做成协程安全的？？
  // 在当前线程上创建主协程。线程如果要创建协程就要先执行这个函数
  static void Init();
  // 协程入口函数（makecontext的第二个参数）
  static void Run();

  // 设置当前执行的协程
  static void SetCurrent(Fiber *fiber);
  // 获取当前正在执行的协程的智能指针，如果不存在，则在当前线程上创建主协程（通过调init实现）。
  static sptr GetCurrent();
  // 获取当前协程 id
  static uint64_t GetCurrentID();
  // 获取当前协程状态
  static State GetCurrentState();
  // 获取存在的协程数量
  static uint32_t TotalFibers();

private:
  uint32_t m_id;         // 协程 id
  uint64_t m_stack_size; // 协程栈大小
  State m_state;         // 协程状态
  ucontext_t m_ctx;      // 当前协程上下文
  void *m_stack;         // 协程栈空间指针
  FiberFunc m_callback;  // 协程执行函数
};

} // namespace meha

// #endif