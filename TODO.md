# 整改

- [ ] 日志：框架默认的日志器是core，用户程序默认的日志器是root，并同步修改流式宏
- [ ] 将 string 换成 string_view
- [ ] 不要滥用assert，assert只用于调试，不是用于正式的错误处理。release代码中assert不会被编译
- [ ] 重构Timer

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