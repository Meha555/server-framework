## 配置模块

用于定义/声明配置项，从配置文件中加载用户配置，并在配置发生变化时自动重新加载。

### TODO

- [ ] 加入命令行参数
- [ ] 添加正则匹配

### 设计思想

- 约定优于配置的设计思想，让程序所依赖的配置项都有一个默认值，就不需要每次都指定了。
- 配置项发生更改时能自动通知（通过触发回调的方式实现）
- 利用第三方库实现文件解析和类型转换

### YAML格式

- **数组（Sequence ）** ：一组按次序排列的值，又称为序列（sequence） / 列表（list），一组以区块格式（Block Format） **（即“破折号+空格”）** 开头的数据组成一个数组

  ```yaml
  port: 5050
  # 支持多层嵌套
  system:
      port: 5050
      addr: 127.0.0.1
  #  支持流式风格
  system: { port: 5050, addr: 127.0.0.1 }
  ```

- **数组（Sequence ）** ：一组按次序排列的值，又称为序列（sequence） / 列表（list），一组以区块格式（Block Format） **（即“破折号+空格”）** 开头的数据组成一个数组

  ```yaml
  values:
    - value1
    - value2
    - value3
  # 支持多维数组（用缩进表示层级关系）
  values:
    -
      - value1
      - value2
    -
      - value3
      - value4
  ```

- **纯量（scalars）** ：单个的、不可再分的值

  ```yaml
  字符串
  布尔值
  整数
  浮点数
  Null
  时间
  日期
  ```

### 代码实现

- `ConfigItemBase`：所有配置项的虚基类，提供 `toString/fromString` 公共接口来实现序列化和反序列化，定义了配置项的名字和描述
- `ConfigItem`：所有配置项的实现类（模板），配置项的值由模板形参决定，配有 `toString/fromString` 方法的实现
- `Config`：解析YAML文件，监听配置项更改，管理所有配置项的单例函数类（没有成员变量，所有的成员函数均为 `static`）
  - `Lookup`：一系列静态方法，用于查询指定的配置项，并在不存在该配置项时自动创建
- `meha::utils::lexical_cast`：封装 `boost::lexical_cast` 的类型转换函数为仿函数模板，并为各YAML数据类型特化实现自己的版本，从而实现YAML数据类型和 `string` 的相互转换，用于支撑实现 `toString/fromString` 方法

如果需要和其他模块结合来实现其他模块的配置文件，就需要其他模块提供一个配置的结构体/类，用于映射具体的配置项。然后这个类还需提供和 `std::string` 一起全特化的 `lexical_cast` 来实现序列化和反序列化。

### 总结

通过`YAML`配置文件可以配置系统的参数，当设置新值时，可以通过回调函数更新系统配置。举个实例讲述一下过程：

1. 在main之前就通过`static LogIniter __log_init;` 添加了配置文件的根结点和通知变化的回调函数
2. 使用`YAML::Node root = YAML::LoadFile("config/log.yml");` 加载文件
3. 使用 `Config::LoadFromYAML(root)` 初始化配置模块，并会调用 `fromString()` 将解析出的 `node` 从 `string` 转化为相应的类型，其中会调用 `setValue` 设置参数值并且调用变化回调函数更新 `logger` 的参数。