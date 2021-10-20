# threadtask
php多线程任务，优点是占用内存少且稳定，对于并行任务处理也有灵活的应用。

### 编译并运行实例代码
* php线程安全模式的编译配置为 --enable-maintainer-zts --with-tsrm-pthreads **php8: --enable-zts**
* 编译php时要关闭外部gd库，即不能加--with-external-gd选项，可以使用--with-external-gd=no代替也行
* 运行测试脚本：./threadtask init.php
* php.ini配置中opcache.protect_memory=0，如果设置为1会导航段错误，原因：这个参数是非线程安全的开关
* 重启信号SIGUSR1,SIGUSR2: kill -SIGUSR1 pid 或者在init.php中使用task_wait(SIGUSR1)

### 函数说明

#### 1. 任务相关
* 创建任务: create_task(string $taskname, string $filename, array $params, string $logfile = '', string $logmode = 'ab', resource &$res = null)
  * $taskname: string 任务名称
  * $filename: string php文件的完整/相对路径，相当于php命令中的file
  * $params: array cli参数，与php filename arg1 arg2 arg3 ...命令中的参数[arg1,arg2,arg3...]类似
  * $logfile: string 输出写入到日志文件
  * $logmode: string 打开文件的模式
  * $res: resource 用于等待任务完成
* 任务是否已完成函数: task_is_run($res)
  * $res: resource 由create_task的第6个引用传递的参数$res而来
* 等待任务完成函数: task_join($res)
  * $res: resource 由create_task的第6个引用传递的参数$res而来
* 向所有线程发送$signal信号，并等待所有线程结束: task_wait($signal)
  * $signal: int 进程信号，如: SIGINT,SIGTERM,SIGUSR1,SIGUSR2
* php运行结束或异常中断自启延时秒数: task_get_delay()
* php异常运行时等待$delay秒后自动重试任务(默认值为1): task_set_delay($delay)
  * $delay: int 秒数
* 获取线程/任务数: task_get_num($is_max = false)
  * $is_max: bool 如果为true则返回最大线程数，否则返回当前线程/任务数
* task_get_threads函数是task_get_num函数的别名
* 设置最大线程数限制(默认值为256): task_set_threads($threads)
  * $threads: int 最大线程数
* 是否开启调试信息: task_get_debug()
* 设置调试信息开关(默认值为true): task_set_debug($isDebug)
  * $isDebug: bool 是否开启调试信息
* 是否正在运行: task_get_run()
* 设置运行状态(默认值为true): task_set_run($isRun = false)
  * $isRun: bool 是否继续运行
* 设置线程信号掩码: pthread_sigmask(int $how, array $set, ?array &oldset = null)
  * $how: 包括以下可选值
    * SIG_BLOCK: 把信号加入到当前阻塞信号中。
    * SIG_UNBLOCK: 从当前阻塞信号中移出信号。
    * SIG_SETMASK: 用给定的信号列表替换当前阻塞信号列表。
  * $set: 信号列表。
  * $oldset: 是一个输出参数，用来返回之前的阻塞信号列表数组。

#### 2. 共享变量相关
* 初始化共享变量(只能在主线程中使用): share_var_init($size = 128)
  * $size: int 初始化变量数
* 是否存在指定的共享变量: share_var_exists($key1[,...])
* 读取共享变量: share_var_get([$key1,...])
* 读取并删除共享变量: share_var_get_and_del([$key1,...])
* 写入共享变量(至少一个参数，每个参数代码要查询的多维数组的key，最后一个是数组可与存在数组合并，否则则替换): share_var_put(...)
* 累加共享变量($key[,...]查到的变量：是数组则会把$value附加到数组后，是字符串则在其后附加$value字符串，其它数值类型或布尔值则会按数值累加): share_var_inc($key[,...],$value)
  * 返回运算结果
* 写入共享变量: share_var_set($key[,...], $value)
* 写入过期共享变量: share_var_set_ex($key[,...], $value, $expire)
  * $expire: int 过期时间戳，为0时永不过期
* 删除共享变量: share_var_del($key1[,...])
* 清空共享变量: share_var_clean()
* 清理已过期共享变量: share_var_clean_ex($expire)
  * $expire: int 过期时间戳，必须大于0
* 统计变量(返回：大于0为数组元素数，小于0为字符长度，true为对象，未找到为null，否则为false): share_var_count([$key1,...])
* 回收共享变量(只能在主线程中使用): share_var_destory()

#### 3. 线程安全的共享变量
* 声明线程安全的共享变量: ts_var_declare(string|int|null $varname, ?resource $var = null, bool $is_fd = false): resource|bool
  * $varname: 变量名，为空则引用$var
  * $var: 如果为空，则为根变量
  * $is_fd: 如果为true，则可以使用ts_var_fd()函数
* 导出socket文件描述符的管道对（可使用sockets扩展中的函数进行操作）：ts_var_fd(resource $var, bool $is_write = false): socket|bool
  * $var: 由ts_var_declare函数返回的变量
  * $is_write: 是返回
* 设置共享变量的过期时间：ts_var_expire(resource $var, int $expire)
  * $var: 由ts_var_declare函数返回的变量
  * $expire: int 过期时间戳，为0时永不过期
* 是否存在指定的共享变量：ts_var_exists(resource $var, string|int $key)
  * $var: 由ts_var_declare函数返回的变量
  * $key: 键名，可为字符串和整形
* 向线程安全变量中存储数据：ts_var_set(resource $var, string|int|null $key, mixed $val, bool $expire = 0): bool
  * $var: 由ts_var_declare函数返回的变量
  * $key: 键名，可为字符串、整形或空，为空时把$val附加到最后
  * $val: 值
  * $expire: 过期时间戳，为0时永不过期
* ts_var_put是ts_var_set的别名
* 压入队列：ts_var_push(resource $var, mixed $val ...): bool
  * $val ...: 同时可以压入多个值
* 弹出队列（线程安全变量）中最后一个：ts_var_pop(resource $var, string|long &$key = null)
  * $key: 是弹出值对应的键
* 弹出队列（线程安全变量）中第一个：ts_var_shift(resource $var, string|long &$key = null)
  * $key: 是弹出值对应的键
* 获取最小或最大键/值：ts_var_minmax(resource $var, bool $is_max = false, bool $is_key = false, string|long &$key = null)
  * $key: 是弹出值对应的键
* 获取线程安全变量数据：ts_var_get(resource $var, string|int|null $key = null, bool $is_del = false): mixed
  * $var: 由ts_var_declare函数返回的变量
  * $key: 键名，可为字符串、整形或空，为空时返回$var中的所有变量
  * $is_del: 是否删除该变量
* 获取线程安全变量，如果不存在则通过回调函数获取数据并设置：ts_var_get_or_set(resource $var, string|int $key, callable $callback, int $expire = 0, mixed $parameters ...): mixed
  * $var: 由ts_var_declare函数返回的变量
  * $key: 键名，可为字符串或整形
  * $callback: 将被调用的回调函数
  * $expire: 过期时间戳，为0时永不过期
  * $parameters: 0个或以上的参数，被传入回调函数
  * **注意：**使用CFLAGS=-DLOCK_TIMEOUT=1 make进行编译可以调试死锁，性能有些差
* 删除线程安全变量中的数据：ts_var_del(resource $var, string|int $key): bool
  * $var: 由ts_var_declare函数返回的变量
  * $key: 键名，可为字符串或整形
* 自增线程安全变量并返回：ts_var_inc(resource $var, string|int|null $key, mixed $inc): mixed
  * $var: 由ts_var_declare函数返回的变量
  * $key: 键名，可为字符串或整形
  * $inc: 相当于$var[$key] += $inc
* 获取线程安全变量有多少个数据（与count函数类似）：ts_var_count(resource $var)
  * $var: 由ts_var_declare函数返回的变量
* 清理线程安全变量并返回元素个数：ts_var_clean(resource $var, int $expire = 0)
  * $var: 由ts_var_declare函数返回的变量
* 重建线程安全变量索引：ts_var_reindex(resource $var, bool $only_integer_keys = false): bool
  * $var: 由ts_var_declare函数返回的变量
  * $only_integer_keys: 是否紧整数索引

#### 4. sockets附加函数
* 导出socket文件描述符为整型: socket_export_fd(resource $socket, bool $is_close = false)
  * $socket: resouce socket_create或socket_create_listen返回的资源类型值
  * $is_close: bool 为true时，不自动关闭$socket文件描述符，使用socket_close($socket)也一样
* 从整型导入socket资源类型值: socket_import_fd(int $fd)
  * $fd: int 来自socket_export_fd的返回的整型值
* 接受socket连接: socket_accept_ex(int $sockfd, string &$addr, int &$port)
  * $sockfd: int 来自socket_export_fd的返回的整型值
  * $addr: string 客户端IP地址
  * $port: int 客户端端口号

#### 5. mysqli附加函数
* 导出mysqli文件描述符为整型: mysqli_export_fd(mysqli $mysql): int|false
* 异步执行mysqli预处理SQL: mysqli_stmt_async_execute(mysqli_stmt $stmt): bool
* 获取mysqli预处理SQL的执行结果: mysqli_stmt_reap_async_query(mysqli_stmt $stmt): bool

#### 6. 代码块执行超时处理
* 设置超时: set_timeout(int $seconds = 1): bool
* 清除超时: clear_timeout(): bool
* 触发超时: trigger_timeout(int $signal = SIGALRM): bool
  * $signal: 向任务线程发送的信号，使用 pcntrl_signal()函数绑定信号处理函数
* 示例: http-server.php中有使用

#### 7. PHP行为函数
* go: go(is_callable $call, $args...): mixed
* 调用并清除由register_shutdown_function注册的php中止函数: call_and_free_shutdown()
* 重新定义常量：redefine(string $name, mixed $value, bool $case_insensitive)

### 常量
* THREAD_TASK_NAME: string 任务名

### 使用示例
* 简单任务控制: ./threadtask init.php
* 等待任务完成: ./threadtask sem.php
* 共享变量: ./threadtask var.php [threads [seconds [type]]]
* 线程安全变量: ./threadtask var2.php [threads [seconds [type]]]
* ini配置加载: ./threadtask ini.php demo.ini
  * SIGUSR1: 重启进程(restart)
  * SIGUSR2: 重载配置(reload)
* 过期共享变量: ./threadtask expire.php
* TCP服务: ./threadtask socket.php
* HTTP服务(两种方式启动HTTP服务命令):
  1. 需要pcntl+sockets扩展: ./threadtask http-server.php [&lt;host&gt; [&lt;port&gt; [flag]]]
  2. 需要pcntl+libevent+sockets扩展: ./threadtask http-server-event.php [&lt;host&gt; [&lt;port&gt; [flag]]]
  ** 参数说明 **
    * host: IP地址
    * port: 监听端口号
    * flag: 非0时每次连接使用create_task创建一次任务，否则默认启动100个常驻任务处理连接，当超过时进行非0时的启动任务。
* yii-app-basic(在yii-app目录执行该命令来启动HTTP服务): ../threadtask http-server.php
  * mysql的test数据库创建，如下：
```sql
CREATE DATABASE IF NOT EXISTS `test` DEFAULT CHARSET=utf8;
CREATE TABLE `test`.`user` (
  `uid` int(11) NOT NULL AUTO_INCREMENT,
  `username` varchar(20) NOT NULL,
  `email` varchar(100) NOT NULL,
  `password` varchar(32) NOT NULL,
  `salt` varchar(8) NOT NULL,
  `registerTime` datetime NOT NULL,
  `loginTime` datetime DEFAULT NULL,
  `loginTimes` int(11) NOT NULL DEFAULT 0,
  PRIMARY KEY (`uid`),
  UNIQUE KEY `username` (`username`),
  UNIQUE KEY `email` (`email`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
INSERT INTO `test`.`user` VALUES
  (1,'admin','admin@test.com','cb13131a512ff854c8bc0dc0ba04e4db','12345678','2019-10-14 22:13:55','2021-03-24 14:49:48',7),
  (2,'test','test@test.com','0ee08e4a9e574f4afa0abfb5ca4e47f8','87654321','2019-10-14 22:13:55','2021-03-24 08:37:56',1),
  (3,'test2','test2@test.com','66b5a5d70de6e691aa9e011eb40bf62c','853532e8','2019-10-16 20:29:18',NULL,0),
  (4,'test3','test3@test.com','093865fe1fc39dedc288275781c12bfe','d03db269','2019-10-16 20:30:10',NULL,0),
  (5,'test4','test4@test.con','94e5d07b62a291858b6cdc902c30f924','cf34c642','2021-03-24 06:40:52','2021-03-24 08:13:17',1),
  (6,'test5','test5@test.com','178a46704b93cd1a6468fe81fc66ae55','f66966f9','2021-03-24 08:17:16','2021-03-24 08:17:54',1);
/* 所有用户的密码都是123456 */
```
  * 使用ab进行压力测试，结果如下：ab -n 5000 -c 100 -k -l http://127.0.0.1:5000/site/user
    * apache+php+mysql+redis: 5000个请求总用14.264秒，吞吐量350.53，内存占用竟然超过了5G，如果apache使用mpm_event_module和php ZTS的话，两者内存占用基本一致
    * threadtask+php+mysql: 5000个请求总用4.354秒，吞吐量1148.33，内存占用665.07MB
    * 总结：使用threadtask性能提升了2倍以上，效果很佳

### v2.0开发计划
  * SocketServer: TCP+UDP协议服务类
  * HttpServer: HTTP+WebSocket服务类
  * HttpRequest: HTTP请求类
  * HttpResponse: HTTP响应类
  * TsVar: 线程间变量共享类
  * Queue：消息队列类
  * ThreadPool: 线程池任务控制类
  * ThreadTask: 线程任务类
