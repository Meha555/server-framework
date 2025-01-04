#include <sys/mman.h>

#include "config.hpp"
#include "fiber.h"
#include "log.h"
#include "scheduler.h"
#include "sem.h"
#include "utils.h"
#include <fmt/format.h>

namespace meha
{

static Logger::sptr g_logger = GET_LOGGER("root");

// 注意这两个不是thread_local的，因为希望让整个程序中的协程号都递增，因此也必须是atmoic的来保证线程安全
// 最后一个协程的id，单调递增用于在整个程序的生命周期内生成协程id，因此存在协程数量限制
static std::atomic_uint64_t s_fiber_id{0};
// 当前协程的数量
static std::atomic_uint64_t s_fiber_count{0};

// 当前线程正在执行的协程（用于执行子协程）
static thread_local Fiber::sptr t_current_fiber{nullptr};
// 当前线程的主协程（用于切回主协程）
static thread_local Fiber::sptr t_master_fiber{nullptr};

// 协程栈大小配置项（默认协程栈空间为128KB）
static ConfigItem<uint64_t>::sptr g_fiber_stack_size{Config::Lookup<uint64_t>("fiber.stack_size", 128 * 1024, "单位:B")};

/**
 * @brief 对 malloc/free 简单封装的内存分配器接口
 * @details 封装一个通用的内存分配器的目的是用于以后的测试性能工作
 * 这里没用多态是因为static方法不属于类的成员，而virtual只能用于成员方法
 */
struct MallocStackAllocator
{
    static void *Alloc(uint64_t size)
    {
        return malloc(size);
    }
    static void Dealloc(void *ptr, uint64_t size)
    {
        free(ptr);
    }
};

struct MMapStackAllocator
{
    static void *Alloc(uint64_t size)
    {
        return mmap(nullptr, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    static void Dealloc(void *ptr, uint64_t size)
    {
        munmap(ptr, size);
    }
};

// 协程栈空间分配器。起别名的作用是以后栈空间分配器换了别的，只需要更换这里的类型就行
using StackAllocator = MallocStackAllocator;

Fiber::Fiber()
    : m_id(0)
    , m_stack_size(0)
    , m_state(EXEC)
    , m_ctx()
    , m_stack(nullptr)
    , m_callback(nullptr)
{
    // 这里创建的是主协程，主协程直接就是开跑的，且不使用我们创建的内存空间来做协程栈，且不存在协程函数
    // 获取上下文对象的副本
    ASSERT_FMT(getcontext(&m_ctx) == 0, "获取主协程上下文失败");
    // 为了确保主协程只创建一次，这里不能为t_master_fiber赋值，应该在Init()中做
    SetCurrent(shared_from_this());
    // 总协程数量增加
    ++s_fiber_count;
    // 主协程号在各自线程中均为0，子协程号才是全局累加的
    LOG_DEBUG(g_logger, "创建主协程[0]");
}

Fiber::Fiber(FiberFunc callback, bool scheduled, size_t stack_size)
    : m_id(++s_fiber_id)
    , m_state(INIT)
    , m_ctx()
    , m_stack(nullptr)
    , m_callback(std::move(callback))
    , m_scheduled(scheduled)
{
    // 注意这里创建的是子协程
    m_stack_size = stack_size == 0 ? g_fiber_stack_size->getValue() : stack_size;
    ASSERT(getcontext(&m_ctx) == 0);
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
    m_scheduled = rhs.m_scheduled;
    // REVIEW 这里用swap比用move能处理callback中存在智能指针的问题？？
    m_callback.swap(rhs.m_callback);
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
        m_scheduled = rhs.m_scheduled;
        // REVIEW 这里用swap比用move能处理callback中存在智能指针的问题？？
        m_callback.swap(rhs.m_callback);
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
    if (m_stack) { // 存在栈，说明是子协程，释放申请的协程栈空间
        ASSERT(m_state == INIT || m_state == TERM);
        StackAllocator::Dealloc(m_stack, m_stack_size);
    } else { // 否则是主协程
        ASSERT(m_state == EXEC);
        if (t_current_fiber == shared_from_this()) {
            SetCurrent(nullptr);
        }
    }
    --s_fiber_count;
}

void Fiber::reset(FiberFunc &&callback) noexcept
{
    ASSERT(m_stack);
    ASSERT(m_state == INIT || m_state == TERM);
    m_callback = std::move(callback);
    // 获取当前的协程上下文
    ASSERT(getcontext(&m_ctx) == 0);
    // 修改当前协程上下文
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stack_size;
    makecontext(&m_ctx, &Fiber::Run, 0);
    m_state = INIT;
}

void Fiber::resume()
{
    // 当前执行的是主协程
    ASSERT(m_state == INIT || m_state == READY);
    SwapFromTo(m_scheduled ? Scheduler::GetSchedulerFiber() : t_master_fiber, shared_from_this());
    // swapOutof(m_scheduled ? Scheduler::GetSchedulerFiber() : t_master_fiber.get());
}

void Fiber::yield()
{
    // 当前执行的是子协程
    ASSERT(m_state == EXEC || m_state == TERM);
    // 协程运行完之后会自动yield一次，用于回到主协程，此时状态已为TERM状态，不应该改为READY
    if (m_state != TERM) {
        m_state = READY; // 准备好下次被换入
    }
    SwapFromTo(shared_from_this(), m_scheduled ? Scheduler::GetSchedulerFiber() : t_master_fiber);
    // swapInto(m_scheduled ? Scheduler::GetSchedulerFiber() : t_master_fiber.get());
}

void Fiber::SwapFromTo(Fiber::sptr from, Fiber::sptr to)
{
    ASSERT(from);
    ASSERT(to);
    SetCurrent(to);
    if (swapcontext(&(from->m_ctx), &(to->m_ctx))) {
        throw SystemError(fmt::format("swap from fiber[{}] to fiber[{}] failed", from->m_id, to->m_id));
    }
}

// void Fiber::swapOutof(Fiber *old_fiber)
// {
//     ASSERT(old_fiber);
//     old_fiber->m_state = READY;
//     SetCurrent(this); // 标记当前运行的协程是谁，目前就在这里写吧，其他地方不太好写这句【 因为这里是主协程执行的，所以写this】
//     LOG(g_logger, TRACE) << "swapIn m_state = EXEC;" << m_id;
//     if (swapcontext(&(old_fiber->m_ctx), &m_ctx)) {
//         throw SystemError(fmt::format("swapIn to fiber[{}] failed", old_fiber->m_id));
//     }
// }

// void Fiber::swapInto(Fiber *new_fiber)
// {
//     ASSERT(new_fiber);
//     // 协程运行完之后会自动yield一次，用于回到主协程，此时状态已为TERM状态
//     if (m_state != TERM) {
//         m_state = READY; // 准备好下次被换入
//     }
//     SetCurrent(new_fiber); // 标记当前运行的协程是谁，目前就在这里写吧，其他地方不太好写这句【 因为这里是子协程执行的，所以写子协程对应的ptr】
//     LOG(g_logger, TRACE) << "swapOut m_state = EXEC;" << new_fiber->m_id << " " << this;
//     if (swapcontext(&m_ctx, &(new_fiber->m_ctx))) {
//         throw SystemError(fmt::format("swapOut off fiber[{}] failed", new_fiber->m_id));
//     }
// }

void Fiber::Init()
{
    if (t_master_fiber == nullptr) {
        t_master_fiber.reset(new Fiber());
    } else {
        LOG_WARN(g_logger, "不能重复创建主协程！");
    }
}

void Fiber::SetCurrent(Fiber::sptr fiber)
{
    t_current_fiber = fiber;
    fiber->m_state = EXEC;
}

Fiber::sptr Fiber::GetCurrent()
{
    // 当前线程还没有创建协程
    if (t_current_fiber == nullptr) {
        Init();
    }
    return t_current_fiber->shared_from_this();
}

uint32_t Fiber::TotalFibers()
{
    return s_fiber_count;
}

uint64_t Fiber::GetCurrentID()
{
    if (t_current_fiber) {
        return t_current_fiber->m_id;
    }
    return 0;
}

Fiber::State Fiber::GetCurrentState()
{
    ASSERT(t_current_fiber != nullptr);
    return t_current_fiber->m_state;
}

uint32_t Fiber::Yield()
{
    uint32_t fid = t_current_fiber->m_id;
    t_current_fiber->yield();
    return fid;
}

void Fiber::Run()
{
    auto current_fiber = GetCurrent(); // 这里会导致引用计数+1
    current_fiber->m_callback(); // 调用协程回调
    current_fiber->m_callback = nullptr;
    current_fiber->m_state = TERM;
    // 维护状态机的收尾工作
    // 执行结束后，切回主协程
    Fiber *current_fiber_ptr = current_fiber.get();
    current_fiber.reset(); // 此时current_fiber的引用计数应该是2，手动reset来减1
    LOG_FMT_TRACE(g_logger, "当前是 %ld 协程\n", current_fiber_ptr->id());
    LOG(g_logger, DEBUG) << "curr: " << current_fiber_ptr
                         << "\nmaster: " << t_master_fiber.get()
                         << "\nscheduler: " << Scheduler::GetSchedulerFiber();
    if (current_fiber_ptr != t_master_fiber.get()) {
        current_fiber_ptr->yield(); // 自动yield返回主协程
    }
}

} // namespace meha
