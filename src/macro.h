#pragma once

/* -------------------------------------------------------------------------------------------------------- */
/*          把宏整理到一个头文件中，保持了宏本身的纯洁性（不参与编译，只在预处理阶段展开），这样就能减少头文件的引入          */
/*               不过最好还是在macro.h中引入相应的头文件，避免要在每个引入macro.h的源文件中自行引入的麻烦               */
/* -------------------------------------------------------------------------------------------------------- */

#include "pch.h"

#if defined __GNUC__ || defined __llvm__
// LIKELY 宏告诉编译器优化,条件大概率成立
#define MEHA_LIKELY(x) __builtin_expect(!!(x), 1)
// UNLIKELY 宏告诉编译器优化,条件大概率不成立
#define MEHA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define NHOOK
#define MEHA_LIKELY(x) (x)
#define MEHA_UNLIKELY(x) (x)
#endif

#define UNUSED(x) (void)(x)

// TODO 改成使用cmake.in来自动生成命令行宏

// 普通断言
#ifndef NDEBUG
#define ASSERT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            LOG_FMT_FATAL(core,                                                    \
                          "Assertion: " #cond "\nSysErr: %s (%u)\nBacktrace:\n%s", \
                          ::strerror(errno),                                       \
                          errno,                                                   \
                          meha::utils::BacktraceToString().c_str());               \
            assert(cond);                                                          \
        }                                                                          \
    } while (0)
#else
#define ASSERT(cond)
#endif

// 额外信息的断言
#ifndef NDEBUG
#define ASSERT_FMT(cond, fmt, args...)                                                      \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            LOG_FMT_FATAL(core,                                                             \
                          "Assertion: " #cond ", " fmt "\nSysErr: %s (%u)\nBacktrace:\n%s", \
                          ##args,                                                           \
                          ::strerror(errno),                                                \
                          errno,                                                            \
                          meha::utils::BacktraceToString().c_str());                        \
            assert(cond);                                                                   \
        }                                                                                   \
    } while (0)
#else
#define ASSERT_FMT(cond, fmt, args...)
#endif

#define MEHA_PTR_INSIDE_CLASS(ClassName)            \
public:                                             \
    using sptr = std::shared_ptr<ClassName>;        \
    using csptr = std::shared_ptr<const ClassName>; \
    using wptr = std::weak_ptr<ClassName>;          \
    using cwptr = std::weak_ptr<const ClassName>;   \
    using uptr = std::unique_ptr<ClassName>;        \
    using cuptr = std::unique_ptr<const ClassName>;

#define MEHA_CLASS_OUTSIDE(ClassName)                               \
    class ClassName;                                                \
    using ClassName##Ptr = std::shared_ptr<ClassName>;              \
    using ClassName##ConstPtr = std::shared_ptr<const ClassName>;   \
    using ClassName##WeakPtr = std::weak_ptr<ClassName>;            \
    using ClassName##ConstWeakPtr = std::weak_ptr<const ClassName>; \
    using ClassName##UPtr = std::unique_ptr<ClassName>;             \
    using ClassName##ConstUPtr = std::unique_ptr<const ClassName>

#define MEHA_STRUCT_OUTSIDE(StructName)                               \
    struct StructName;                                                \
    using StructName##Ptr = std::shared_ptr<StructName>;              \
    using StructName##ConstPtr = std::shared_ptr<const StructName>;   \
    using StructName##WeakPtr = std::weak_ptr<StructName>;            \
    using StructName##ConstWeakPtr = std::weak_ptr<const StructName>; \
    using StructName##UPtr = std::unique_ptr<StructName>;             \
    using StructName##ConstUPtr = std::unique_ptr<const StructName>
