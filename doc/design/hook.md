## Hook模块

### 设计思想

hook系统底层和socket相关的API，socket IO相关的API，以及sleep系列的API。通过hook模块，可以使一些不具异步功能的API，展现出异步的性能，如MySQL。

通过hook，可以利用本框架中的协程来实现异步。

注意：本篇提到的系统调用接口实际是指C标准函数库提供的接口，而不是单指Linux提供的系统调用，比如malloc和free就不是系统调用，它们是C标准函数库提供的接口，封装了系统调用。

### Hook概述

#### 什么是Hook

hook实际上就是对原API进行一次封装，将其封装成一个与原API同签名的接口，其实现一般是先执行封装中的操作，再执行原API。子类重写父类方法也可以理解为是一种Hook。

hook的优点：

- 接口不变，降低学习成本；
- 能够扩展别人的库，增加新的功能。

hook的缺点：

- 开发者本身不清楚自己使用的是原API还是Hook后的API，容易造成困惑；

- 影响性能；

- 以异步hook为例，如果异步的hook调用太多，则会造成回调地狱。

- 为了确保不影响原API的语义，可能要Hook额外的API：

  > hook的重点是在替换API的底层实现的同时完全模拟其原本的行为，因为调用方是不知道hook的细节的，在调用被hook的API时，如果其行为与原本的行为不一致，就会给调用方造成困惑。比如，所有的socket fd在进行IO协程调度时都会被设置成`NONBLOCK`模式，如果用户未显式地对fd设置`NONBLOCK`，那就要处理好`fcntl`，不要对用户暴露fd已经是`NONBLOCK`的事实，这点也说明，除了IO相关的函数要进行hook外，对`fcntl`, `setsockopt`之类的功能函数也要进行hook，才能保证API的一致性。

#### 为什么需要Hook模块

有时想让库函数在实现原有功能的同时，提供其他功能。本项目想要提供的就是异步功能。

> 常见的hook加入的功能有：引用计数、协议通信、埋点、asan等

注意：本项目的hook是和IO协程密切相关的，如果不使用IO协程，则hook毫无意义。

##### 场景讲解

本项目的hook的目的是在不重新编写代码的情况下，把老代码中的socket IO相关的API都转成异步，以提高性能。

考虑IOManager要在一个线程上按顺序调度以下协程：

1. 协程1：sleep(2) 睡眠两秒后返回。
2. 协程2：在scoket fd1 上send 100k数据。
3. 协程3：在socket fd2 上recv直到数据接收成功。

在未hook的情况下，IOManager要调度上面的协程，流程是下面这样的：

1. 调度协程1，协程阻塞在sleep上，等2秒后返回，这两秒内调度线程是被协程1占用的，其他协程无法在当前线程上调度。
2. 调度协徎2，协程阻塞send 100k数据上，这个操作一般问题不大，因为send数据无论如何都要占用CPU时间，但如果fd迟迟不可写，那send会阻塞直到套接字可写，导致在阻塞期间其他协程也无法在当前线程上调度。
3. 调度协程3，协程阻塞在recv上，这个操作要直到recv超时或是有数据时才返回，期间调度器也无法调度其他协程。

上面的调度流程最终总结起来就是，协程只能按顺序调度，一旦有一个协程阻塞住了，那整个调度线程也就阻塞住了，其他的协程都无法在当前线程上执行。其根本原因是这些API都是同步的，且执行单位是线程。

---

像这种完全阻塞的方式其实并不是完全不可避免，以sleep为例，调度器完全可以在检测到协程sleep后，将协程yield以让出执行权，同时设置一个定时器，2秒后再将协程重新resume。这样，调度器就可以在这2秒期间调度其他的任务，同时还可以顺利的实现sleep 2秒后再继续执行协程的效果，send/recv与此类似。在完全实现hook后，IOManager的执行流程将变成下面的方式：

1. 调度协程1，检测到协程sleep，那么先添加一个2秒的定时器，定时器回调函数是在调度器上继续调度本协程，接着协程yield，等定时器超时。
2. 因为上一步协程1已经yield了，所以协徎2并不需要等2秒后才可以执行，而是立刻可以执行。同样，调度器检测到协程send，由于不知道fd是不是马上可写，所以先在IOManager上给fd注册一个写事件，回调函数是让当前协程resume并执行实际的send操作。然后当前协程yield，等待可写事件发生。
3. 上一步协徎2也yield了，可以马上调度协程3。协程3与协程2类似，也是给fd注册一个读事件，回调函数是让当前协程resume并继续recv，然后当前协程yield，等待可读事件发生。
4. 等2秒超时后，执行定时器回调函数，将协程1 resume以便继续执行。
5. 等协程2的fd可写，一旦可写，调用写事件回调函数将协程2 resume以便继续执行send。
6. 等协程3的fd可读，一旦可读，调用回调函数将协程3 resume以便继续执行recv。

上面的4、5、6步都是异步的，调度线程并不会阻塞，IOManager仍然可以调度其他的任务，只在相关的事件发生后，再继续执行对应的任务即可。并且，由于hook的函数签名与原函数一样，所以对调用方也很方便，只需要以同步的方式编写代码，实现的效果却是异步执行的，效率很高。

总而言之，在IO协程调度中对相关的系统调用进行hook，可以让调度线程尽可能得把CPU时间片都花在有意义的操作上，而不是浪费在阻塞等待中。

#### 如何实现Hook

这里只讲解动态链接中的hook实现，静态链接以及基于内核模块的hook不在本章讨论范围。

- 对于静态函数：需要在链接原API所在动态库时，先链接自己Hook实现的动态库（通过 `LD_PRELOAD` 环境变量）；
- 对于成员函数：一般hook虚函数，需要改写虚函数表。

---

hook的实现机制非常简单，就是通过动态库的全局符号介入功能，用自定义的接口来替换掉同名的系统调用接口。由于系统调用接口基本上是由C标准函数库libc提供的，所以这里要做的事情就是用自定义的动态库来覆盖掉libc中的同名符号。

**注意**：一旦当前翻译单元内的符号被Hook了，那么不通过 `dlsym` 是无法调用到原来的符号的。【有待验证，但应该是这样】

##### 非侵入式Hook

通过优先加自定义载动态库来实现对后加载的动态库进行hook，这种hook方式不需要重新编译代码，考虑以下例子：

**main.c**

```c
#include <unistd.h>
#include <string.h>
 
int main() {
    write(STDOUT_FILENO, "hello world\n", strlen("hello world\n")); // 调用系统调用write写标准输出文件描述符
    return 0;
}
```

在这个例子中，可执行程序调用write向标准输出文件描述符写数据。对这个程序进行编译和执行，效果如下：

```shell
# gcc main.c
# ./a.out
hello world
```

使用ldd命令查看可执行程序的依赖的共享库，如下：

```shell
# ldd a.out
        linux-vdso.so.1 (0x00007ffc96519000)
        libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007fda40a61000)
        /lib64/ld-linux-x86-64.so.2 (0x00007fda40c62000)
```

可以看到其依赖libc共享库，write系统调用就是由libc提供的。

gcc编译生成可执行文件时会默认链接libc库，所以不需要显式指定链接参数，这点可以在编译时给 gcc 增加一个 "-v" 参数，将整个编译流程详细地打印出来进行验证，如下：

```shell
# gcc -v main.c
Using built-in specs.
COLLECT_GCC=gcc
COLLECT_LTO_WRAPPER=/usr/lib/gcc/x86_64-linux-gnu/9/lto-wrapper
OFFLOAD_TARGET_NAMES=nvptx-none:hsa
OFFLOAD_TARGET_DEFAULT=1
Target: x86_64-linux-gnu
...
 /usr/lib/gcc/x86_64-linux-gnu/9/collect2 -plugin /usr/lib/gcc/x86_64-linux-gnu/9/liblto_plugin.so -plugin-opt=/usr/lib/gcc/x86_64-linux-gnu/9/lto-wrapper -plugin-opt=-fresolution=/tmp/ccZQ60eg.res -plugin-opt=-pass-through=-lgcc -plugin-opt=-pass-through=-lgcc_s -plugin-opt=-pass-through=-lc -plugin-opt=-pass-through=-lgcc -plugin-opt=-pass-through=-lgcc_s --build-id --eh-frame-hdr -m elf_x86_64 --hash-style=gnu --as-needed -dynamic-linker /lib64/ld-linux-x86-64.so.2 -pie -z now -z relro /usr/lib/gcc/x86_64-linux-gnu/9/../../../x86_64-linux-gnu/Scrt1.o /usr/lib/gcc/x86_64-linux-gnu/9/../../../x86_64-linux-gnu/crti.o /usr/lib/gcc/x86_64-linux-gnu/9/crtbeginS.o -L/usr/lib/gcc/x86_64-linux-gnu/9 -L/usr/lib/gcc/x86_64-linux-gnu/9/../../../x86_64-linux-gnu -L/usr/lib/gcc/x86_64-linux-gnu/9/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/x86_64-linux-gnu -L/usr/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/9/../../.. /tmp/ccnT2NOd.o -lgcc --push-state --as-needed -lgcc_s --pop-state -lc -lgcc --push-state --as-needed -lgcc_s --pop-state /usr/lib/gcc/x86_64-linux-gnu/9/crtendS.o /usr/lib/gcc/x86_64-linux-gnu/9/../../../x86_64-linux-gnu/crtn.o
COLLECT_GCC_OPTIONS='-v' '-mtune=generic' '-march=x86-64'
```

注意上面的 `"/usr/lib/gcc/x86_64-linux-gnu/9/collect2 ... -pop-state -lc -lgcc ..."`，这里的 **`-lc`** 就说明程序在进行链接时会自动链接一次libc。

下面在不重新编译代码的情况下，用自定义的动态库来替换掉可执行程序a.out中的write实现，新建hook.c，内容如下：

**hook.c**

```c
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
 
ssize_t write(int fd, const void *buf, size_t count) {
    syscall(SYS_write, STDOUT_FILENO, "12345\n", strlen("12345\n"));
}
```

这里实现了一个write函数，这个函数的签名和libc提供的write函数完全一样，函数内容是用syscall的方式直接调用编号为SYS_write的系统调用，实现的效果也是往标准输出写内容，只不过这里我们将输出内容替换成了其他值。将hook.c编译成动态库：

```shell
gcc -fPIC -shared hook.c -o libhook.so
```

通过设置 `**LD_PRELOAD**`环境变量，将libhoook.so设置成优先加载，从面覆盖掉libc中的write函数，如下：

```shell
# LD_PRELOAD="./libhook.so" ./a.out
12345
```

这里我们并没有重新编译可执行程序a.out，但是可以看到，write的实现已经替换成了我们自己的实现。究其原因，就是LD_PRELOAD环境变量，它指明了在运行a.out之前，系统会优先把libhook.so加载到了程序的进程空间，使得在a.out运行之前，其全局符号表中就已经有了一个write符号，这样在后续加载libc共享库时，由于全局符号介入机制，libc中的write符号不会再被加入全局符号表，所以全局符号表中的write就变成了我们自己的实现。

##### 侵入式Hook

需要改造代码或是重新编译一次以指定动态库加载顺序。如果是以改造代码的方式来实现hook，那么可以像下面这样直接将write函数的实现放在main.c里，那么编译时全局符号表里先出现的必然是main.c中的write符号：

**main.c**

```c
#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
 
ssize_t write(int fd, const void *buf, size_t count) {
    syscall(SYS_write, STDOUT_FILENO, "12345\n", strlen("12345\n"));
}
 
int main() {
    write(STDOUT_FILENO, "hello world\n", strlen("hello world\n")); // 这里调用的是上面的write实现
    return 0;
}
```

如果不改造代码，那么可以重新编译一次，通过编译参数将自定义的动态库放在libc之前进行链接。由于默认情况下gcc总会链接一次libc，并且libc的位置也总在命令行所有参数后面，所以只需要像下面这样操作就可以了：

```shell
# gcc main.c -L. -lhook -Wl,-rpath=.
# ./a.out
12345
```

这里显式指定了链接libhook.so（`-Wl,-rpath=.`用于指定运行时的动态库搜索路径，避免找不到动态库的问题），由于libhook.so的链接位置比libc要靠前（可以通过gcc -v进行验证），所以运行时会先加载libhook.so，从而实现全局符号介入，这点也可以通过ldd命令来查看：

```shell
# ldd a.out
        linux-vdso.so.1 (0x00007ffe615f9000)
        libhook.so => ./libhook.so (0x00007fab4bae3000)
        libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007fab4b8e9000)
        /lib64/ld-linux-x86-64.so.2 (0x00007fab4baef000)
```

关于hook的另一个讨论点是如何找回已经被全局符号介入机制覆盖的系统调用接口，这个功能非常实用，因为大部分情况下，系统调用提供的功能都是无可替代的，我们虽然可以用hook的方式将其替换成自己的实现，但是最终要实现的功能，还是得由原始的系统调用接口来完成。

以malloc和free为例，假如我们要hook标准库提供的malloc和free接口，以跟踪每次分配和释放的内存地址，判断有无内存泄漏问题，那么具体的实现方式应该是，先调用自定义的malloc和free实现，在分配和释放内存之前，记录下内存地址，然后再调用标准库里的malloc和free，以真正实现内存申请和释放。

上面的过程涉及到了查找后加载的动态库里被覆盖的符号地址问题。首先，这个操作本身就具有合理性，因为程序运行时，依赖的动态库无论是先加载还是后加载，最终都会被加载到程序的进程空间中，也就是说，那些因为加载顺序靠后而被覆盖的符号，它们只是被“雪藏”了而已，实际还是存在于程序的进程空间中的，通过一定的办法，可以把它们再找回来。在Linux中，这个方法就是`dslym`，它的函数原型如下：

```c
#define _GNU_SOURCE
#include <dlfcn.h>
 
void *dlsym(void *handle, const char *symbol);
```

关于dlsym的使用可参考`man 3 dlsym`，在链接时需要指定 `-ldl` 参数。使用dlsym找回被覆盖的符号时，第一个参数固定为 `RTLD_NEXT`，第二个参数为符号的名称，下面通过dlsym来实现上面的内存跟踪功能：

**hook_malloc.c**

```c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
 
typedef void* (*malloc_func_t)(size_t size);
typedef void (*free_func_t)(void *ptr);
 
// 这两个指针用于保存libc中的malloc和free的地址
malloc_func_t sys_malloc = NULL;
free_func_t sys_free = NULL;
 
// 重定义malloc和free，在这里重定义会导致libc中的同名符号被覆盖
// 这里不能调用带缓冲的printf接口，否则会出段错误
void *malloc(size_t size) {
    // 先调用标准库里的malloc申请内存，再记录内存分配信息，这里只是简单地将内存地址和长度打印出来
    void *ptr = sys_malloc(size);
    fprintf(stderr, "malloc: ptr=%p, length=%ld\n", ptr, size);
    return ptr;
}
void free(void *ptr) {
    // 打印内存释放信息，再调用标准库里的free释放内存
    fprintf(stderr, "free: ptr=%p\n", ptr);
    sys_free(ptr);
}
 
int main() {
    // 通过dlsym找到标准库中的malloc和free的符号地址
    sys_malloc = dlsym(RTLD_NEXT, "malloc");
    assert(dlerror() == NULL);
    sys_free = dlsym(RTLD_NEXT, "free");
    assert(dlerror() == NULL);
 
    char *ptrs[5];
 
    for(int i = 0; i < 5; i++) {
        ptrs[i] = malloc(100 + i);
        memset(ptrs[i], 0, 100 + i);
    }
     
    for(int i = 0; i < 5; i++) {
        free(ptrs[i]);
    }
    return 0;
}
```

编译运行以上代码，效果如下：

```shell
# gcc hook_malloc.c -ldl
# ./a.out
malloc: ptr=0x55775fa8e2a0, length=100
malloc: ptr=0x55775fa8e310, length=101
malloc: ptr=0x55775fa8e380, length=102
malloc: ptr=0x55775fa8e3f0, length=103
malloc: ptr=0x55775fa8e460, length=104
free: ptr=0x55775fa8e2a0
free: ptr=0x55775fa8e310
free: ptr=0x55775fa8e380
free: ptr=0x55775fa8e3f0
free: ptr=0x55775fa8e460
```

### Hook模块设计

sylar的hook功能是**线程粒度**的，可自由设置当前线程是否使用hook。默认情况下，协程调度器的调度线程会开启hook，而其他线程则不会开启。

sylar对以下函数进行了hook，并且只对socket fd进行了hook，如果操作的不是socket fd，那会直接调用系统原本的API，而不是hook之后的API：

```c
sleep
usleep
nanosleep
socket
connect
accept
read
readv
recv
recvfrom
recvmsg
write
writev
send
sendto
sendmsg
close
fcntl
ioctl
getsockopt
setsockopt
```

除此外，sylar还增加了一个 `ConnectWithTimeout` 接口用于实现带超时的connect。

为了管理所有的socket fd，sylar设计了一个`FdManager`类来记录所有分配过的fd的上下文，这是一个单例类，每个socket fd上下文记录了当前fd的读写超时，是否设置非阻塞等信息。

关于hook模块和IO协程调度的整合，一共有三类接口需要hook，如下：

- sleep延时系列接口，包括sleep/usleep/nanosleep。对于这些接口的hook，需要**给IO协程调度器注册一个定时事件，在定时事件超时触发后再继续执行当前协程。当前协程在注册完定时事件后即可yield让出执行权。**

- socket IO系列接口，包括read/write/recv/send...等，connect及accept也可以归到这类接口中。这类接口的hook首先需要判断操作的fd是否是socket fd，以及用户是否显式地对该fd设置过非阻塞模式，如果不是socket fd或是用户显式设置过非阻塞模式，那么就不需要hook了，直接调用操作系统的IO接口即可。如果需要hook，那么需要**在IO协程调度器上注册对应的读写事件，等事件就绪发生后再继续执行当前协程。当前协程在注册完IO事件即可yield让出执行权。**

- socket/fcntl/ioctl/close等接口，这类接口主要处理的是边缘情况，比如分配fd上下文，处理超时及用户显式设置非阻塞的情况。

在每个被hook的IO函数的实现中，需要插入构造协程、构造IO事件监听的逻辑。为了批量实现这个过程，添加了 `doIO` 静态函数来实现代理地调用hook后的协程版本和hook前的原来版本，以及配套的宏来批量生成代码。

`doIO` 这个函数内部主要是判断该线程内hook功能是否开启，并且根据fd是否打开、是否由用户手动设置了非阻塞来选择是否使用原来版本的API。

通过超时管理和取消操作来判断如果是资源不足，则会添加一个事件监听器来等待资源可用。同时，如果有超时设置，还会启动一个条件计时器来取消事件。超时事件主要是处理当超时事件到达后，还没处理就处理它。然后添加一个事件读或写事件来监听相应的就绪事件并调用协程处理。

所以 `doIO` 是对 `read/write` 等API的非阻塞协程封装。具体在于如果出现资源暂时不可用，则添加对应的就绪事件监听来将当前协程挂起，然后在 `epoll_wait` 中使用超时事件/触发就绪事件来 `tickle` 对应的协程，将任务放到调度器中执行，然后取消对应的超时事件/就绪事件。