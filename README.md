# C++高性能服务器框架

## 简介

仿[sylar: C++高性能分布式服务器框架](https://github.com/sylar-yin/sylar)的基于C++17实现的高性能服务器框架。

参考资料：

- [C++高性能服务器框架 – SYLAR – 02日志模块 – sylar的博客](http://www.sylar.top/blog/?p=147)
- [C++_古猜..的博客-CSDN博客](https://blog.csdn.net/qq_35099224/category_12613947.html)
- [日志模块 - 类库与框架 - 程序员的自我修养 (midlane.top)](https://www.midlane.top/wiki/pages/viewpage.action?pageId=10061019#id-日志模块-LogEventWrap)
- [sylar C++高性能服务器框架——日志模块 - 掘金 (juejin.cn)](https://juejin.cn/post/7241821748211777593#heading-51)

## 项目分析

本项目编译生成静态库 `libconet.a`，测试代码在tests下面。

### 日志模块

日志模块：`Logger` 输出日志，是现场构造一个 `LogEvent` ，然后经过 `LogFormatter`，再由 `LogAppender` 输出到目的地。

- `LogEvent`：封装后的日志消息（即日志的元数据）
  - `LogLevel`：日志等级
- `LogFormatter`：日志格式化器
- `LogAppender`：日志输出器
  - `StdoutLogAppender`：标准输出日志输出器
  - `FileLogAppender`：文件日志输出器
- `Logger`：日志器，组合使用上述组件
- `LogConfig`：日志器配置信息类
- `LogAppenderConfig`：日志输出器的配置信息类
- `LogEventWrapper`：日志的RAII风格打印（析构时打印日志）

### 配置模块

用于定义/声明配置项，从配置文件中加载用户配置，并在配置发生变化时自动重新加载

- `ConfigItemBase`：所有配置项的虚基类，提供 `toString/fromString` 公共接口，定义了配置项的名字和描述
- `ConfigItem`：所有配置项的实现类（模板），配置项的值由模板形参决定，配有 `toString/fromString` 方法的实现
- `Config`：解析YAML文件，监听配置项更改，管理所有配置项的单例函数类（没有成员变量，所有的成员函数均为 `static`）
- `meha::lexical_cast`：封装 `boost::lexical_cast` 的类型转换函数为仿函数模板，并为各YAML数据类型特化实现自己的版本，从而实现YAML数据类型和 `string` 的相互转换，用于支撑实现 `toString/fromString` 方法

- [ ] 加入命令行参数
- [ ] 添加正则匹配

### 线程模块

> 为什么不直接使用C++11提供的thread类。按sylar的描述，因为thread其实也是基于pthread实现的。并且C++11里面没有提供读写互斥量，RWMutex，Spinlock等，在高并发场景，这些对象是经常需要用到的，所以选择自己封装pthread。

`Thread`：线程类，构造函数传入线程入口函数和线程名称，线程入口函数类型为void()，如果带参数，则需要用`std::bind`进行绑定。线程类构造之后线程即开始运行，构造函数在线程真正开始运行之后返回。

线程同步类（这部分被拆分到mutex.hpp）中：

- `Semaphore`: 计数信号量，基于`sem_t`实现
- `Mutex`: 互斥锁，基于`pthread_mutex_t`实现
- `RWMutex`: 读写锁，基于`pthread_rwlock_t`实现
- `Spinlock`: 自旋锁，基于`pthread_spinlock_t`实现
- `CASLock`: 原子锁，基于`std::atomic_flag`实现

注意：

- 关于线程入口函数。sylar的线程只支持void(void)类型的入口函数，不支持给线程传参数，但实际使用时可以结合`std::bind`来绑定参数，这样就相当于支持任何类型和数量的参数。

- 关于子线程的执行时机。sylar的线程类可以保证在构造完成之后线程函数一定已经处于运行状态，这是通过一个信号量来实现的，构造函数在创建线程后会一直阻塞，直到线程函数运行并且通知信号量，构造函数才会返回，而构造函数一旦返回，就说明线程函数已经在执行了。

- 关于线程局部变量。sylar的每个线程都有两个线程局部变量，一个用于存储当前线程的Thread指针，另一个存储线程名称，通过Thread::GetThis()可以拿到当前线程的指针。一般都要求线程有名称

- 关于范围锁。sylar大量使用了范围锁来实现互斥，范围锁是指用类的构造函数来加锁，用析造函数来释放锁。这种RAII方式可以简化锁的操作，也可以避免忘记解锁导致的死锁问题。

TODO:

- [ ] 添加线程池（sylar没做线程池是由于该框架主要用协程做高并发的任务）
- [ ] 合理设定创建线程和协程失败时是否是致命错误

### 协程模块

#### 协程类

> [协程模块 - 类库与框架 - 程序员的自我修养 (midlane.top)](https://www.midlane.top/wiki/pages/viewpage.action?pageId=10060957#id-协程模块-ucontext_t接口)

协程是一种看起来花里胡哨，并且使用起来也花里胡哨的函数，用于在单线程中暂时让出CPU（yield）和获得CPU（resume）来执行不同的代码块，从而实现单线程内的并发。协程切换时主要保存的上下文环境是指寄存器的内容、栈帧的内容。

因此，协程只是利用所属线程的CPU核心来单核并发，并不能像线程那样利用多核并行。属于用户态调度的执行流。

优点：

- 跨平台体系结构
- 无需线程上下文切换的开销（相比线程切换）
- 无需原子操作锁定及同步的开销（相比多线程程序）
- 方便切换控制流，简化编程模型（调用与回调可以在同一个地方写完）
- 高并发+高扩展性+低成本：高性能CPU可以启用非常多的协程，很适合用于高并发处理。

缺点：

- 无法利用多核资源：协程的本质是个单线程，它不能将一个**多核处理器**的的多个核同时用上,协程需要和进程配合才能运行在多CPU上。（线程、多核、超线程参见CSAPP第三版1.9.2并发和并行P17）当然我们日常所编写的绝大部分应用都没有这个必要，除非是cpu密集型应用。

- 进行阻塞（Blocking）操作（如IO时）会阻塞掉整个线程

> [【协程第一话】协程到底是怎样的存在？_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1b5411b7SD)
>
> [【协程第二话】协程和IO多路复用更配哦~_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1a5411b7aZ)

##### 按照调用栈分——有栈协程和无栈协程

> [有栈协程与无栈协程 (mthli.xyz)](https://mthli.xyz/stackful-stackless/)

这里的“栈”**并不是说这个协程运行的时候有没有栈空间，而是说协程之间是否存在函数调用栈，也称执行栈**。因为但凡是个正在运行的程序，不管是协程，还是线程，怎么可能在运行的时候不使用栈空间来创建栈上变量呢。

- 有栈协程（stackful coroutine）：
  - ⽤执行栈来保存协程的上下文信息，相当于用户态线程。切换的成本是用户态线程切换的成本
  - 当协程被挂起时保存当前上下文，并将控制权交还给调度器。当协程被恢复时，栈协程会将之前保存的执行状态恢复，从上次挂起的地⽅继续执行。类似于内核态线程的实现，不同协程间切换还是要切换对应的栈上下文，只是不⽤陷⼊内核⽽已。

- 无栈协程（stackless coroutine）：
  - 没有执行栈来保存协程的上下文信息，相当于可挂起/恢复的函数。切换的成本则相当于函数调用的成本
  - 当协程被挂起时，⽆栈协程会将协程的状态保存在堆上内存中，并将控制权交还给调度器。当协程被恢复时，⽆栈协程会将之前保存的状态从堆中取出，并从上次挂起的地⽅继续执⾏。协程切换时，使⽤状态机来切换，就不⽤切换对应的上
    下⽂了，因为都在堆⾥的。比有栈协程都要轻量许多。

**注意**：

- 不管是有栈还是无栈，其协程栈都在所属线程的堆中，区别只在于调用栈是否共享。
- 由于无栈协程不改变调用栈，因此（几乎）不可能在任意一个嵌套函数中挂起无栈协程，因为既然没有保存栈，则当前多出来的栈帧必须执行完。而有栈协程则没有这个限制，可以在任意嵌套函数中挂起。不过由于不需要切换栈帧，无栈协程的性能倒是比有栈协程普遍要高一些。

##### 按照执行栈分——独立栈和共享栈

独立栈和共享栈都是有栈协程。注意栈空间大小一旦分配后续不可更改。

- 共享栈：所有的协程在运行的时候都使⽤同⼀个，大家都可以读写。
  - 让出CPU时，将共享栈中的内容拷贝出来，保存的时候需要⽤到多少内存就开辟多少，这样就减少了内存的浪费；
  - 恢复该协程的时候，将协程之前保存的栈内容重新拷贝到执行栈中；
  - 使⽤的公共资源内存⽐较大，相对安全，不存在内存碎片，但是协程频繁切换需要进⾏拷贝，耗费CPU。

- 独立栈：每个协程的栈空间都是自己独立的，不会给其他协程共享。
  - 好处是协程切换的时候不用发生内存的拷贝（因为上下文已经在自己的独立栈中了）；
  - 坏处则是内存空间浪费较多。因为栈空间在运行时不能随时扩容，否则如果有指针操作执行了栈内存，扩容后将导致指针失 效。为了防止栈内存不够，每个协程都要预先开⼀个足够大的栈空间使⽤，当实际没有使用这么多内存时就造成了浪费；
  - 相对简单，但浪费内存，容易栈溢出。 

##### 按照调度方式分——对称协程和非对称协程

- 对称协程（asymmetric coroutine）：
  - 协程间的调度是任意的，即协程间地位平等，协程与其原调用者没有关系。
  - 实现一般不含协程调度器，协程不仅要运行自己的入口函数代码，还要负责选出下一个合适的协程进行切换，相当于每个协程都要充当调度器

- 非对称协程（symmetric coroutine）：
  - 协程让出CPU时只能让回给原调用者。即存在“调用堆栈”的结构
  - 实现一般包含协程调度器


|                           对称协程                           |                          非对称协程                          |
| :----------------------------------------------------------: | :----------------------------------------------------------: |
|           协程让出CPU后，可以换入其他任何协程执行            | 协程让出CPU后，只能换入其调用者执行，而调度者一般是协程调度器，然后由调度器调度下一个协程执行 |
|                     只存在 `invoke` 操作                     |      协程自己只能执行 `yield` 操作，调度器执行 `resume`      |
| ![image](https://img2024.cnblogs.com/blog/3077699/202407/3077699-20240721123340450-1780313018.png) | ![image](https://img2024.cnblogs.com/blog/3077699/202407/3077699-20240721121444668-589512670.png) |

sylar实现的是独立栈的有栈非对称协程。

##### 并发安全

由于协程涉及到执行流，因此并发访问临界区自然需要加锁。此时的锁应当是协程级别的锁，才能避免不同协程之间的并发安全。

由于协程们运行在同一个线程中，因此禁止在协程中重复加线程级别的锁，否则就是同一线程内递归上锁导致死锁了。

##### 协程原语

协程除了创建语句外，只有两种操作，一种是resume，表示恢复协程运行，一种是yield，表示让出执行。协程的结束没有专门的操作，协程函数运行结束时协程即结束，协程结束时会自动调用一次yield以返回主协程。

##### 协程上下文

协程能够半路yield、再重新resume的关键是协程存储了函数在yield时间点的执行状态，这个状态称为协程上下文。协程上下文包含了函数在当前执行状态下的全部CPU寄存器的值，这些寄存器值记录了函数栈帧、代码的执行位置等信息，如果将这些寄存器的值重新设置给CPU，就相当于重新恢复了函数的运行。在Linux系统里这个上下文用 `ucontext_t` 结构体表示，配有对应的API。

##### 协程调度

同样是单线程环境下，协程的yield和resume一定是同步进行的，一个协程的yield，必然对应另一个协程的resume，因为线程不可能没有执行主体。并且，协程的yield和resume是完全由应用程序来控制的。与线程不同，线程创建之后，线程的运行和调度也是由操作系统自动完成的，但协程创建后，协程的运行和调度都要由应用程序来完成。

- 所谓创建协程，其实就是把一个函数包装成一个协程对象，然后再用起协程的方式运行这个函数；
- 所谓协程调度，其实就是创建一批的协程，然后再创建一个调度协程来调度执行这些协程（协程可以在被调度时继续向调度器添加新的调度任务）；
- 所谓IO协程调度，其实就是在调度协程时，如果发现这个协程在等待IO就绪，那就先让这个协程让出执行权，等对应的IO就绪后再重新恢复这个协程的运行；
- 所谓定时器，就是给调度协程预设一个协程对象，等定时时间超时就恢复预设的协程对象。

##### 协程状态

仿照进程的5状态（创建态、就绪态、运行态、阻塞态、终止态）模型，将协程状态简化为4个状态：

```mermaid
graph LR
INIT -->|第一次调入执行| READY
READY -->|resume:获得处理机调入执行| EXEC
EXEC -->|yield:放弃处理机加入调度| READY
EXEC -->|协程函数运行结束| TERM
```

- 创建态INIT：协程刚创建，还没有开始执行（只相当于标记位，调入会直接变到EXEC）
- 就绪态READY：协程函数还没有开始执行（可能是刚创建，或被挂起）
- 运行态EXEC：协程函数正在执行
- 终止态TERM：协程函数运行结束

设计成这样是基于几点考虑：

1. INIT和TERM是由于reset重置协程函数不应在协程函数已经执行过的情况下重置，因此需要区分状态
2. 由于协程属于用户态调度执行的，隶属于线程，因此不应调用线程原语阻塞，所谓的协程阻塞行为应该由用户态调度器`Scheduler`自行管理和实现

##### 协程模块设计

本部分是基于glibc的 `ucontext_t` 实现非对称协程。即**子协程只能和主协程切换，而不能和另一个子协程切换。由主协程选出一个子协程来调度运行，而不允许子协程有调度其他协程的能力**。并且在程序结束时，一定要再切回主协程，以保证程序能正常结束，像下面这样：

![image](https://img2024.cnblogs.com/blog/3077699/202406/3077699-20240610230857545-699286863.png)

sylar使用线程局部变量（C++11`thread_local`变量）来保存协程上下文对象，这点很好理解，因为协程是在线程里运行的，不同线程的协程相互不影响，每个线程都要独自处理当前线程的协程切换问题。

对于每个线程的协程上下文，sylar设计了两个线程局部变量来存储协程上下文信息（对应源码的`t_current_fiber`和`t_master_fiber`），也就是说，一个线程在任何时候最多只能知道两个协程的上下文（当前执行的和主协程的），前者用于调度执行当前协程，后者用于切回主协程。

sylar的非对称协程代码实现简单，并且在后面实现协程调度时可以做到公平调度，缺点是子协程只能和线程主协程切换，意味着子协程无法创建并运行新的子协程，并且在后面实现协程调度时，完成一次子协程调度需要先切换回主协程，这会额外多切换一次上下文。

##### 协程模块实现

> [linux ucontext族函数的原理及使用_ucontext排查短促-CSDN博客](https://blog.csdn.net/qq_44443986/article/details/117739157)

（1）`ucontext_t` 结构体定义

```c
// 上下文结构体是平台相关的，因为不同平台的寄存器不一样
// 下面列出的是所有平台都至少会包含的4个成员
typedef struct ucontext_t {
    struct ucontext_t *uc_link;   // 当前上下文结束后，下一个激活的上下文对象的指针，只在当前上下文是由makecontext创建时有效
    sigset_t          uc_sigmask; // 当前上下文的信号屏蔽掩码
    stack_t           uc_stack;   // 当前上下文使用的栈内存空间，只在当前上下文是由makecontext创建时有效
    mcontext_t        uc_mcontext;// 平台相关的上下文具体内容，包含寄存器的值
    ...
} ucontext_t;

// 寄存器值结构体
typedef struct {
    gregset_t __ctx(gregs);   // 所装载寄存器
    fpregset_t __ctx(fpregs); // 寄存器的类型
} mcontext_t;

// 栈内存结构体
typedef struct {
    void *ss_sp;  // 栈顶指针
    int ss_flags; // 备用栈状态
    size_t ss_size; // 栈空间大小
} stack_t;
```

（2）配套API

> 注意不能存在某时刻没有执行实体

```c
// 获取当前的上下文
int getcontext(ucontext_t *ucp);
 
// 恢复ucp指向的上下文，这个函数不会返回，而是会跳转到ucp上下文对应的函数中执行，相当于调用了函数
int setcontext(const ucontext_t *ucp);
 
// 修改由getcontext获取到的上下文指针ucp，将其与一个函数func进行绑定，支持指定func运行时的参数。
// 在调用makecontext之前，必须手动给ucp分配一段内存空间，存储在ucp->uc_stack中，这段内存空间将作为func函数运行时的栈空间；
// 同时也可以指定ucp->uc_link，表示函数运行结束后恢复uc_link指向的上下文；如果不赋值uc_link，那func函数结束时必须手动调用setcontext或swapcontext以重新指定一个有效的上下文，否则程序就没有执行流了
// makecontext执行完后，ucp就与函数func绑定了，调用setcontext或swapcontext激活ucp时，func就会被运行
void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);
 
// 恢复ucp指向的上下文，同时将当前的上下文存储到oucp中，
// 和setcontext一样，swapcontext也不会返回，而是会跳转到ucp上下文对应的函数中执行，相当于调用了函数
// swapcontext是非对称协程实现的关键，线程主协程和子协程用这个接口进行上下文切换
int swapcontext(ucontext_t *oucp, const ucontext_t *ucp);
```

#### 协程调度器

实现了一个 $N:M$ 的协程调度器，$N$ 个线程运行 $M$ 个协程，协程可以在线程之间进行切换，也可以绑定到指定线程运行。

##### 为什么需要协程调度器

> 在前面的协程模块中，对于每个协程，都需要用户手动调用协程的resume方法将协程运行起来，然后等协程yield并返回，再运行下一个协程。这种运行协程的方式其实是用户自己充当调度器，非常麻烦。

实现协程调度之后，可以先创建一个协程调度器，然后把要调度的协程传递给调度器，由**调度器负责创建线程来调度执行这些协程**。还能解决协程模块中子协程不能直接运行另一个子协程的缺陷，子协程可以通过向调度器添加调度任务的方式来运行另一个子协程。

##### 一些设计层面的考虑

**调度任务**：

- 这里认为所有可运行的实体都是调度任务，但是以协程为基本执行单位（这些协程称为任务协程）。因此函数要想执行，必须得先构造为协程
- 此外，还涉及到添加调度任务，发现调度任务，通知任务完成

**三类协程**：

- 主协程：原本的程序执行流
- 调度协程：负责调度任务协程的协程
- 任务协程：执行任务的协程

**工作线程**：

- 执行任务的任务协程和负责调度任务协程的调度者协程都是需要跑在线程里的，这个线程叫做工作线程

**多线程加速**：

- 一个线程同一时刻只能运行一个协程，而如果有多个线程就意味着有多个协程可以同时执行，因此协程调度器如果支持在多个线程内调度协程执行任务协程显然能大幅提升效率。【暂不考虑多个任务线程的负载均衡】
- 线程切换存在开销，因此线程数要尽可能少。如果能尽可能利用调度器所在线程那就可以白嫖一个线程作为调度线程（主协程）【这里认为每个调度协程都有一个调度协程】

因此，`Scheduler` 分为：

- 任务队列：需要调度执行的任务
- 调度线程池：调度器事先创建好的线程池，其中线程作为调度线程。每当在存在调度任务时调度线程的调度协程从任务队列中取出任务执行，没有任务时就阻塞等待。执行完所有任务后随着调度器整体销毁。

|                    不利用调度者线程的情况                    |                     利用调度者线程的情况                     |
| :----------------------------------------------------------: | :----------------------------------------------------------: |
| ![image](https://img2024.cnblogs.com/blog/3077699/202407/3077699-20240721215332627-702521131.png) | ![image](https://img2024.cnblogs.com/blog/3077699/202407/3077699-20240721215406824-345601220.png) |
| 用单独的调度线程用于跑调度协程：调度者线程中只有主协程，调度线程中有调度协程和任务协程 | 利用调度器所在线程作为调度线程跑调度协程：调度器所在线程中有主协程、调度协程和任务协程 |
|                          实现较简单                          |                          实现较困难                          |
| 只需要让新线程的入口函数作为调度协程，从任务队列⾥取任务执行即可， 原本调度器所在线程的主协程与调度协程完全不相关，主协程只需要向调度器添加任务，然后在适当的时机停⽌调度器即可。当调度器停⽌时，调度器所在线程要等待调度线程结束后再退出。 | 由于sylar是非对称协程，因此原本不允许调度协程和任务协程直接切换。而主协程本身是没有调度逻辑的，因此应当让调度协程和任务协程之间可以直接切换。因此需要添加第三个线程局部变量，从而同时记录当前执行的协程、主协程、调度协程，确保主协程的信息不丢失。 |

*利用调度者协程的情况费了那么大劲，到底是防止主协程承担调度的逻辑，否则就又回到了原本用户充当调度器的情况。*

**idle如何处理，也就是当调度器没有协程可调度时，调度协程该怎么办**

直觉上来看这里应该有一些同步手段，比如，没有调度任务时，调度协程阻塞住，比如阻塞在一个idle协程上，等待新任务加入后退出idle协程，恢复调度。然而这种方案是无法实现的，因为每个线程同一时间只能有一个协程在执行，如果调度线程阻塞在idle协程上，那么除非idle协程自行让出执行权，否则其他的协程都得不到执行，这里就造成了一个先有鸡还是先有蛋的问题：只有创建新任务idle协程才会退出，只有idle协程退出才能创建新任务。为了解决这个问题，sylar采取了一个简单粗暴的办法，如果任务队列空了，调度协程会不停地检测任务队列，看有没有新任务，俗称忙等待，CPU使用率爆表。这点可以从sylar的源码上发现，一是Scheduler的tickle函数什么也不做，因为根本不需要通知调度线程是否有新任务，二是idle协程在协程调度器未停止的情况下只会yield to hold，而调度协程又会将idle协程重新swapIn，相当于idle啥也不做直接返回。这个问题在sylar框架内无解，只有一种方法可以规避掉，那就是设置autostop标志，这个标志会使得调度器在调度完所有任务后自动退出。在后续的IOManager中，上面的问题会得到一定的改善，并且tickle和idle可以实现得更加巧妙一些，以应对IO事件。

**任务的调度时机**

归纳起来，如果只使用caller线程进行调度，那所有的任务协程都在stop之后排队调度，如果有额外线程，那任务协程在刚添加到任务队列时就可以得到调度。

- 只有main函数线程参与调度时的调度执行时机。

  前面说过，当只有main函数线程参与调度时，可以认为是主线程先攒下一波协程，然后切到调度协程开始调度这些协程，等所有的协程都调度完了，调度协程进idle状态，这个状态下调度器只能执行忙等待，啥也做不了。这也就是说，主线程main函数一旦开启了协程调度，就无法回头了，位于开始调度点之后的代码都执行不到。对于这个问题，sylar把调度器的开始点放在了stop方法中，也就是，调度开始即结束，干完活就下班。IOManager也是类似，除了可以调用stop方法外，IOManager类的析构函数也有一个stop方法，可以保证所有的任务都会被调度到。

- 额外创建了调度线程时的调度执行时机。

  如果不额外创建线程，也就是线程数为1并且use caller，那所有的调度任务都在stop()时才会进行调度。但如果额外创建了线程，那么，在添加完调度任务之后任务马上就可以在另一个调度线程中调度执行。

TODO:

- [ ] 协程调度器
- [ ] 支持内存池

### 定时器模块

采用小根堆实现的定时器。

所有定时器根据绝对的超时时间点进行排序，每次取出离当前时间最近的一个超时时间点，计算出超时需要等待的时间，然后等待超时。超时时间到后，获取当前的绝对时间点，然后把最小堆里超时时间点小于这个时间点的定时器都收集起来，执行它们的回调函数。

**注意**：

- 在注册定时事件时，一般提供的是相对时间，比如相对当前时间3秒后执行。sylar会根据传入的相对时间和当前的绝对时间计算出定时器超时时的绝对时间点，然后根据这个绝对时间点对定时器进行最小堆排序。因为依赖的是系统绝对时间，所以需要考虑校时。
- 定时器的超时等待基于epoll_wait，精度只支持毫秒级，因为epoll_wait的超时精度也只有毫秒级



## 杂谈

**1.方法论**

1.1如何阅读这个大型项目

（1）先建好环境，让程序能运行，玩一遍

当你拿到一个程序后，即使你不看代码，你也应该知道它是干什么的吧！干啥都不知道，不用看了。

（2）看想办法掌握程序的结构

作者微博、Google、百度、PDSN、等找到程序的体系结构，完整的文档

（3）先体系再细节；先平面再线点。

先整体再局部，先平面再线点。"大胆猜测，小心求证"。在阅读程序的过程中，我们往往对某一处或几处逻辑不肯定，可能是这样，也可能是那样。

（4）断点调试、日志调试。

（5）忽略细节，先前不要关注分支

Main开始一字一句的解读，遇到一个问题，直到他认为这个问题已经处理不再是个问题的时候，才进行下一步。

这是一个做事方法问题：这样无疑会导致整个进度的延缓。若我们一开始只关注整体结构（一个宏观的大概的流程），而忽略掉那些细支末节，则很有利。

那些可以忽略呢？

如有些函数一看函数名便知道是干什么的，没有要一开始便深入。

有些系统中的分支（如某此特殊场景下才执行的逻辑）、不重要的功能，则一扫而过

（6）其它

善用搜索引擎

先整体再局部，先宏观再微观，先流程再细节。



1.2.如何阅读大型项目的C++代码

章节0:了解文档

要搞清楚别人的代码，首先，你要了解代码涉及的领域知识，这是最重要的，不懂领域知识，只看代码本身，不可能搞的明白。

其次，你得找各种文档：需求文档（要做什么），设计文档（怎么做的），先搞清楚你即将要阅读是什么玩意，至少要把代码的整体结构搞清楚：整体架构如何，有几个模块，模块间通信方式，运行环境，构建工具等等。如果只关注一部分，重点关注将会属于你的模块，其他代码了解下即可

章节1:熟悉语法

熟悉C++语法与新特性

熟悉命名规范(变量 函数 类等) ->快速了解作用及含义, 有时通过名字可以看出设计模式等 不用深入探索源码也能知道含义 有文档说明最好 没有也可以靠时间与经验发掘.

章节2:摸清架构(需要拆细)

自上而下,了解全貌而非细节, 了解整个系统架构, 重点关注属于你的模块.

章节3:熟悉工具

熟悉相应的生成/测试工具 编译脚本等, 方便熟悉程序整体功能/流程.

章节4:细看模块(接口 算法需要拆细)

找到程序入口,自上而下抽丝剥茧.

章节5:输出产物

有目的性的阅读可以提高干劲,提前讨论好需要输出/补充的内容,按要求补充/输出成果物.

**2.sylar项目的运行:**

https://www.midlane.top/wiki/pages/viewpage.action?pageId=16416843

按照这个老哥的配置先把这个老哥的项目跑起来（这个项目跟sylar老师讲的差不多）

注意Boost和yaml-cpp的安装，其他安装照上面老哥的来

ubuntu下Boost安装：https://blog.csdn.net/qq_36666115/article/details/131015894

ubuntu下yaml-cpp安装与使用：https://www.cnblogs.com/zwjason/p/17031701.html

**3.项目的流程或者结构理解：**

作者主页：http://www.sylar.top

参考2：https://www.midlane.top/wiki/pages/viewpage.action?pageId=16416843

参考3：流程的理解，感觉这个会好一点：https://juejin.cn/post/7241821748211777593#heading-51

## 问题

- [ ] 为什么用shared_ptr不用unique_ptr