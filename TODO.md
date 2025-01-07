# 整改

- [ ] 命令行参数解析换成用getopt系列接口
- [ ] 根据链接关系和用途，划分core\等文件夹
- [ ] mutex.cc中各种锁的初始化中异常的处理
- [ ] 代码合理地添加适当log记录
- [ ] rootlogger改成通过extern获取，不要到处创建static shared_ptr实例
- [ ] 目前Hook模块的配置项依赖于config模块，但是Hook模块有自己的static initer对象，导致其初始化顺序和config模块的static initer对象未知，可能产生未定义行为
- [ ] 日志：框架默认的日志器是core，用户程序默认的日志器是root，并同步修改流式宏
- [ ] 将 string 换成 string_view
- [ ] 不要滥用assert，assert只用于调试，不是用于正式的错误处理。release代码中assert不会被编译
- [ ] 重构Timer，使用标准库chrono代替各种时间、秒数的uint64_t、将锁和信号量类型改成using取的别名，使用标准库线程、然后切一个换成标准库实现的，减少代码量

作为框架和标准库，应当跑异常、panic、abort之类。但是作为业务代码，则不应该跑异常、panic、abort，而是应当处理标准库和框架的这些异常。

# 未来计划

仿照sylar最新的工程，添加一些功能：

- [ ] pimpl设计模式
- [ ] 符号导出可见性
- [ ] doxygen文档和gitbook文档部署
- [ ] 引入内存池，避免大量的alloc/free产生系统调用拖累协程
- [ ] 插件机制
- [ ] orm框架
- [ ] 容器部署
- [ ] redis缓存
- [ ] 分布式数据库