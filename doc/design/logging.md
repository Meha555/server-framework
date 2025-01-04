## 日志模块

### 设计思想

类似于 `spdlog` ，采用封装后的日志事件来提供基本的日志消息，然后解析模板字符串进行格式化，最后使用对应的输出后端来 `sink` 到对应的输出位置。

### TODO

- [ ] 异步日志（目前是实时输出的，所谓的异步日志是加一个线程池，定时或者定量输出日志）
- [ ] 日志分片和轮转
- [ ] 更多的自定义后端（journalctl、syslog、TCP对端）

### 代码实现

日志模块：`Logger` 输出日志，是现场构造一个 `LogMessage` ，然后经过 `LogFormatter`，再由 `LogAppender` 输出到目的地。

日志输出相关：
- `LogMessage`：封装后的日志消息（即日志的元数据）
  - `LogLevel`：日志等级
- `LogFormatter`：日志格式化器
- `LogAppender`：日志输出器
  - `StdoutLogAppender`：标准输出日志输出器
  - `FileLogAppender`：文件日志输出器
- `Logger`：日志器，组合使用上述组件。建议每个模块使用自己的一个日志器（就像Qt的LogCategory一样）
- `LogMessageWrapper`：日志的RAII风格打印（析构时打印日志）

配置文件相关：
- `LogConfig`：日志器配置信息类
- `LogAppenderConfig`：日志输出器的配置信息类
这两个相当于Go项目配置文件的结构体一样，前者会持有后者。

#### 技巧

- 单一职责原则：每个类仅负责很小的一个功能，靠很多类组合在一起完成大的功能和流水线
- 利用RAII
- 利用宏简化代码
- 利用状态机解析模板字符串

### C语言va_list

这里代码没被使用，方法还是可以看一下

1）首先在函数里定义一具`va_list`型的变量`al`，这个变量是指向参数的指。

2）使用 `va_start(al, fmt)` 宏初始化 `al`, 并将其指向参数列表中的第一个参数。

3）将`fmt`和`al`传入`format`中。

4）使用`vasprintf(&buf, fmt, al)`将 `al`按照`fmt格式`放到`buf`中。

5）若成功，则将`buf`输出到流中 。

6）最后使用 `va_end()` 宏清理 `va_list`。

```cpp
void LogMessage::format(const char* fmt, ...) {
    va_list al;  		//1）
	va_start(al, fmt);	//2）
	format(fmt, al);	//3）
	va_end(al);			//6)
}

void LogMessage::format(const char* fmt, va_list al){
	char *buf = nullptr;
    // len返回写入buf的长度
	int len = vasprintf(&buf, fmt, al);	//4）
	if(len != -1) {
		m_ss << std::string(buf, len);	//5）
		free(buf);
	}
}
```

### 总结

日志管理使用单例模式，保证从容器`m_loggers`中拿出的日志器是唯一不变的。日志管理器会初始化一个主日志器放到容器中，若再创建一个新的日志器时没有设置`appender`，则会使用这个主日志器进行日志的输出，但是输出时日志名称并不是主日志器的，因为在输出时是按照`event`中的`logger`的名称输出的。

再梳理一遍当一个日志器被定义再到打印出日志是怎么一个流程：

1. 首先我们使用日志管理器`LoggerMgr`获得一个`logger`，例如这里获得主日志器，可以使用宏`SYLAR_LOG_ROOT()`获得，例如`sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT()`。此时`LoggerManager`会`new`一个新的`Logger`，默认为名字为`root`，`level`为`DEBUG`，`formatter`为`%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n`。并且`addAppend()` 为`StdoutLogAppender`，然后将`root logger`放入日志器容器中。此时，一个日志器就初始化好了。

2. 在过程1中，在初始化`g_logger`时，它的`formatter`也会被`init()`方法初始化，并将解析对应的`FormatItem`按照格式顺序放到`m_items`中。在也会将`StdoutLogAppender`加到`m_appenders`中，在`addAppender()`时，若`appender`没有`formatter`时，会将`g_logger`的`formatter`赋给它作为默认的配置。

3. 当我们想打印日志时，需要创建相应日志级别的`LogMessage`，其中包含了`时间 线程号 线程名称 协程号 [日志级别] [日志器名称] 文件名:行号 消息`

   (e.g.)`2023-04-26 15:12:12 3613 iom 0 [INFO] [root] tests/test_hook.cc:69 hello world`

4. 使用宏定义日志级别为`INFO`的事件`SYLAR_LOG_INFO(g_logger)`，使用方法如下`SYLAR_LOG_INFO(logger) << hello world`。在宏的最后调用`getSS()`获得消息字符，将`"hello world"`保存到`m_ss`中。

5. 由于`LogMessage`是由`LogMessageWarp`包装起来的，当该行结束时，自动执行析构函数语句`m_event->getLogger()->sink(m_event->getLevel(), m_event);`【注意这里没有像 `std::cout` 那样在每个 `operator<<` 中立马输出】

   其中`getLogger()`获得当前日志器`g_logger`，并且调用`g_logger->sink(m_event->getLevel(), m_event)`方法，在此方法中会从`m_appenders`中循环拿出要输出到的目的地，因为这里只有一个`StdoutLogAppender`，所以调用`StdoutLogAppender::ptr->sink(self, level, event)`方法，其中调用`m_formatter->format(*logger*, *level*, *event*)`遍历`formatter`的`m_item`将`event`中的事件按照格式顺序输出到流，然后用`std::cout`打印到控制台，此时日志信息就按照流的方式全部打印出来了。