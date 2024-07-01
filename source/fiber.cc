#include "fiber.h"
#include "exception.h"
#include "log.h"
#include "mutex.hpp"
#include "scheduler.h"
#include "sem.h"
#include "util.h"
#include <sys/mman.h>

namespace meha {

static Logger::ptr g_logger = GET_LOGGER("root");

//注意这两个不是thread_local的，因为希望让整个程序中的协程号都递增，因此也必须是atmoic的来保证线程安全
// 最后一个协程的id，用于生成协程id
static std::atomic_uint64_t s_fiber_id{0};
// 当前协程的数量
static std::atomic_uint64_t s_fiber_count{0};

// 当前线程正在执行的协程（用于执行子协程）
static thread_local Fiber *t_current_fiber{nullptr};
// 当前线程的主协程（用于切回主协程）
static thread_local Fiber::sptr t_master_fiber{nullptr};

// 协程栈大小配置项（默认协程栈空间为1M）
static ConfigItem<uint64_t>::ptr g_fiber_stack_size{
    Config::Lookup<uint64_t>("fiber.stack_size", 1024 * 1024, "单位:B")};

/**
 * @brief 对 malloc/free 简单封装的内存分配器
 * @details 封装一个通用的内存分配器的目的是用于以后的测试性能工作
 * 这里没用多态是因为static方法不属于类的成员，而virtual只能用于成员方法
 */
struct MallocStackAllocator
{
    static void *Alloc(uint64_t size) { return malloc(size); }
    static void Dealloc(void *ptr, uint64_t size) { free(ptr); }
};

struct MMapStackAllocator
{
    static void *Alloc(uint64_t size)
    {
        return mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    static void Dealloc(void *ptr, uint64_t size) { munmap(ptr, size); }
};

// 协程栈空间分配器。起别名的作用是以后栈空间分配器换了别的，只需要更换这里的类型就行
using StackAllocator = MallocStackAllocator;

Fiber::Fiber() : m_id(0), m_stack_size(0), m_state(EXEC), m_ctx(), m_stack(nullptr), m_callback()
{
    // 这里创建的是主协程，主协程直接就是开跑的，且不使用我们创建的内存空间来做协程栈，且不存在协程函数
    // 为了确保主协程只创建一次，这里不能为t_master_fiber赋值，应该在init()中做
    SetThis(this);
    // 获取上下文对象的副本
    if (getcontext(&m_ctx)) {
        throw SystemError("getcontext() error");
    }
    // 总协程数量增加
    ++s_fiber_count;
    LOG_DEBUG(g_logger, "创建主协程[0]");
}

Fiber::Fiber(FiberFunc callback, size_t stack_size)
    : m_id(++s_fiber_id),
      m_state(INIT),
      m_ctx(),
      m_stack(nullptr),
      m_callback(std::move(callback))
{
    // 注意这里创建的是子协程
    // 如果传入的 stack_size 为 0，使用配置项 "fiber.stack_size" 设置的值
    m_stack_size = stack_size == 0 ? g_fiber_stack_size->getValue() : stack_size;
    // 获取上下文对象的副本（构造函数最好不要抛异常）
    ASSERT_FMT(getcontext(&m_ctx) == 0, "get fiber context failed");
    // 给上下文对象分配分配新的栈空间内存
    m_stack = StackAllocator::Alloc(m_stack_size);
    ASSERT_FMT(m_stack, "fiber stack alloc failed");
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stack_size;
    // 给新的上下文绑定入口函数
    makecontext(&m_ctx, &Fiber::Run, 0);

    ++s_fiber_count;
    LOG_FMT_DEBUG(g_logger, "创建子协程[%d]", m_id);
}

Fiber::Fiber(Fiber &&rhs) noexcept
{
    // 移动构造不需要考虑自赋值问题
    m_id = rhs.m_id;
    m_stack_size = rhs.m_stack_size;
    m_state = rhs.m_state;
    m_ctx = rhs.m_ctx;
    m_stack = rhs.m_stack;
    m_callback.swap(rhs.m_callback);  // REVIEW 这里用swap比用move能处理callback中存在智能指针的问题？？
    rhs.m_stack = nullptr;
    rhs.m_stack_size = 0;
    rhs.m_ctx.uc_link = nullptr;
    rhs.m_ctx.uc_stack.ss_sp = nullptr;
    rhs.m_ctx.uc_stack.ss_size = 0;
    LOG_FMT_DEBUG(g_logger, "移动子协程[%d]", m_id);
}

Fiber &Fiber::operator=(Fiber &&rhs) noexcept
{
    if (&rhs != this) {
        StackAllocator::Dealloc(m_stack, m_stack_size);
        m_id = rhs.m_id;
        m_stack_size = rhs.m_stack_size;
        m_state = rhs.m_state;
        m_ctx = rhs.m_ctx;
        m_stack = rhs.m_stack;
        m_callback.swap(rhs.m_callback);  // REVIEW 这里用swap比用move能处理callback中存在智能指针的问题？？
        rhs.m_stack = nullptr;
        rhs.m_stack_size = 0;
        rhs.m_ctx.uc_link = nullptr;
        rhs.m_ctx.uc_stack.ss_sp = nullptr;
        rhs.m_ctx.uc_stack.ss_size = 0;
    }
    LOG_FMT_DEBUG(g_logger, "移动子协程[%d]", m_id);
    return *this;
}

Fiber::~Fiber()
{
    LOG_FMT_DEBUG(g_logger, "析构协程[%d]", m_id);
    if (m_stack) {  // 存在栈，说明是子协程，释放申请的协程栈空间
        ASSERT(m_state == INIT || m_state == TERM || m_state == ERROR);  // 只允许协程在INIT、TERM、ERROR状态被析构
        StackAllocator::Dealloc(m_stack, m_stack_size);
    } else {  // 否则是主协程
        ASSERT(m_state == EXEC);
        if (t_current_fiber == this) {
            SetThis(nullptr);
        }
    }
    --s_fiber_count;
}

void Fiber::reset(FiberFunc &&callback)
{
    ASSERT(m_stack);
    // 执行时不允许变更
    ASSERT(m_state == INIT || m_state == TERM || m_state == ERROR);
    m_callback = std::move(callback);
    //获取当前的协程上下文
    if (getcontext(&m_ctx)) {
        throw SystemError("getcontext() error");
    }
    //修改当前协程上下文
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stack_size;
    makecontext(&m_ctx, &Fiber::Run, 0);
    m_state = INIT;
}

// TODO 这种原语性的操作，在抛异常后，应当具备状态回滚的能力，可以参考KWin的pending实现
// NOTE 这里原语内部不能调原语，比如resume中不能调yield
void Fiber::resume()
{
    ASSERT_FMT(t_master_fiber, "当前必须有主协程");
    ASSERT_FMT(t_master_fiber.get() == t_current_fiber, "当前执行的协程必须是主协程");
    ASSERT(m_state == INIT || m_state == READY || m_state == HOLD);
    t_master_fiber->m_state = HOLD;
    SetThis(this);
    m_state = EXEC;
    if (swapcontext(&(t_master_fiber->m_ctx), &m_ctx)) {
        throw SystemError("swapcontext() error");
    }
}

void Fiber::yield()
{
    ASSERT_FMT(t_master_fiber, "当前必须有主协程");
    SetThis(t_master_fiber.get());
    if (swapcontext(&m_ctx, &(t_master_fiber->m_ctx))) {
        throw SystemError("swapcontext() error");
    }
}

void Fiber::swapIn()
{
    //    assert(Scheduler::GetCurrent()->m_root_thread_id == -1 ||
    //           Scheduler::GetCurrent()->m_root_thread_id != GetThreadID());
    // 只有协程是等待执行的状态才能被换入
    ASSERT(m_state == INIT || m_state == READY || m_state == HOLD);
    SetThis(this);
    m_state = EXEC;
    // 挂起 master fiber，切换到当前 fiber
    // REVIEW 原来用的是这个Scheduler的版本，现在暂时简化为使用主线程本身作为调度器
    // ASSERT_FMT(Scheduler::GetMainFiber(), "请勿手动调用该函数，该函数应由主协程自动调用");
    // if (swapcontext(&(Scheduler::GetMainFiber()->m_ctx), &m_ctx))
    ASSERT_FMT(t_master_fiber, "请勿手动调用该函数，该函数应由主协程自动调用");
    if (swapcontext(&(t_master_fiber->m_ctx), &m_ctx)) {
        throw SystemError("swapcontext() error");
    }
}

void Fiber::swapOut()
{
    //    assert(Scheduler::GetCurrent()->m_root_thread_id == -1 ||
    //           Scheduler::GetCurrent()->m_root_thread_id != GetThreadID());
    // 协程运行完之后会自动yield一次，用于回到主协程，此时状态已为结束状态
    ASSERT(m_state == EXEC || m_state == TERM);
    m_state = HOLD;
    SetThis(t_master_fiber.get());
    // 挂起当前 fiber，切换到 master fiber
    // REVIEW 原来用的是这个Scheduler的版本，现在暂时简化为使用主线程本身作为调度器
    // ASSERT_FMT(Scheduler::GetMainFiber(), "请勿手动调用该函数，该函数应由主协程自动调用");
    // if (swapcontext(&m_ctx, &(Scheduler::GetMainFiber()->m_ctx)))
    ASSERT_FMT(t_master_fiber, "请勿手动调用该函数，该函数应由主协程自动调用");
    if (swapcontext(&m_ctx, &(t_master_fiber->m_ctx))) {
        throw SystemError("swapcontext() error");
    }
}

void Fiber::swapIn(Fiber::sptr fiber)
{
    assert(m_state == INIT || m_state == READY || m_state == HOLD);
    SetThis(this);
    m_state = EXEC;
    if (swapcontext(&(fiber->m_ctx), &m_ctx)) {
        throw SystemError("swapcontext() error");
    }
}

void Fiber::swapOut(Fiber::sptr fiber)
{
    assert(m_state == EXEC);
    m_state = HOLD;
    SetThis(fiber.get());
    if (swapcontext(&m_ctx, &(fiber->m_ctx))) {
        throw SystemError("swapcontext() error");
    }
}

bool Fiber::isFinished() const noexcept { return (m_state == TERM || m_state == ERROR); }

void Fiber::init()
{
    if (t_master_fiber == nullptr) {
        t_master_fiber.reset(new Fiber());
    } else {
        LOG_WARN(g_logger, "不能重复创建主协程！");
    }
}

Fiber::sptr Fiber::GetCurrent()
{
    //当前线程还没有创建协程
    if (t_current_fiber == nullptr) {
        init();
    }
    return t_current_fiber->shared_from_this();
}

void Fiber::SetThis(Fiber *fiber) { t_current_fiber = fiber; }

void Fiber::YieldToReady()
{
    /* FIXME: 可能会造成 shared_ptr 的引用计数只增不减 */
    Fiber::sptr current_fiber = GetCurrent();
    current_fiber->m_state = READY;
    // if (Scheduler::GetCurrent() && Scheduler::GetCurrent()->m_root_thread_id == GetThreadID())
    // { // 调度器实例化时 use_caller 为 true, 并且当前协程所在的线程就是 root thread
    //     // current_fiber->swapOut(Scheduler::GetCurrent()->m_root_fiber);
    //     current_fiber->swapOut(t_master_fiber);
    // }
    // else
    // {
    //     current_fiber->swapOut();
    // }
    current_fiber->yield();
}

void Fiber::YieldToHold()
{
    /* FIXME: 可能会造成 shared_ptr 的引用计数只增不减 */
    GetCurrent()->swapOut();
    // if (Scheduler::GetCurrent() && Scheduler::GetCurrent()->m_root_thread_id == GetThreadID())
    // { // 调度器实例化时 use_caller 为 true, 并且当前协程所在的线程就是 root thread
    //     current_fiber->swapOut(t_master_fiber);
    // }
    // else
    // {
    //     current_fiber->swapOut();
    // }
}

uint32_t Fiber::TotalFibers() { return s_fiber_count; }

uint64_t Fiber::GetCurrentID()
{
    if (t_current_fiber != nullptr) {
        return t_current_fiber->m_id;
    }
    return 0;
}

Fiber::State Fiber::GetCurrentState()
{
    if (t_current_fiber != nullptr) {
        return t_current_fiber->m_state;
    }
    return ERROR;
}

// TODO 这个函数要看一下
void Fiber::Run()
{
    auto current_fiber = GetCurrent();  // 这里会导致引用计数+1
    try {
        current_fiber->m_callback();  // 调用协程回调
        current_fiber->m_callback = nullptr;
        current_fiber->m_state = TERM;
    } catch (const meha::Exception &e) {
        LOG_FMT_ERROR(g_logger, "Fiber exception: %s, call stack:\n%s", e.what(), e.stackTrace());
    } catch (const std::exception &e) {
        LOG_FMT_ERROR(g_logger, "Fiber exception: %s", e.what());
    } catch (...) {
        LOG_ERROR(g_logger, "Fiber exception");
    }
    // 维护状态机的收尾工作
    // 执行结束后，切回主协程
    Fiber *current_fiber_ptr = current_fiber.get();
    current_fiber.reset();  // 此时current_fiber的引用计数应该是2，reset后就是1
    if (Scheduler::GetCurrent() && Scheduler::GetCurrent()->m_caller_thread_id == GetThreadID() &&
        Scheduler::GetCurrent()->m_caller_fiber.get() !=
            current_fiber_ptr) {  // 调度器实例化时 use_caller 为 true, 并且当前协程所在的线程就是 root thread
        // current_fiber_ptr->swapOut(Scheduler::GetCurrent()->m_root_fiber);
        current_fiber_ptr->swapOut();
    } else {
        current_fiber_ptr->yield();  // 自动yield返回主协程
    }
}

}  // namespace meha
