# threadtask
php多线程任务，优点是占用内存少且稳定，对于并行任务处理也有另活的应用。

### 编译并运行实例代码
* php线程安全模式的编译配置为 --enable-maintainer-zts --with-tsrm-pthreads **php8: --enable-zts**
* 运行测试脚本：./threadtask init.php
* php.ini配置中opcache.protect_memory=0，如果设置为1会导航段错误，原因：这个参数是非线程安全的开关
* 重启信号SIGUSR1,SIGUSR2: kill -SIGUSR1 pid 或者在init.php中使用task_wait(SIGUSR1)

### 函数说明
* 创建任务: create_task($taskname, $filename, $params, $logfile = NULL, $logmode = 'ab', &$res = null)
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
* php异常运行时等待$delay秒后自动重试任务(默认值为1): task_set_delay($delay)
  * $delay: int 秒数
* 设置最大线程数限制(默认值为256): task_set_threads($threads)
  * $threads: int 最大线程数
* 设置调试信息开关(默认值为true): task_set_debug($isDebug)
  * $isDebug: bool 是否开启调试信息
* 设置运行状态(默认值为true): task_set_run($isRun = false)
  * $isRun: bool 是否继续运行
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
* 导出socket文件描述符为整型: socket_export_fd(resource $socket, bool $is_close = false)
  * $socket: resouce socket_create或socket_create_listen返回的资源类型值
  * $is_close: bool 为true时，不自动关闭$socket文件描述符，使用socket_close($socket)也一样
* 从整型导入socket资源类型值: socket_import_fd(int $fd)
  * $fd: int 来自socket_export_fd的返回的整型值

### 常量
* THREAD_TASK_NAME: string 任务名
* THREAD_TASK_NUM: int 最大线程数
* THREAD_TASK_DELAY: int php异常运行时等待$delay秒后自动重试任务

### 使用示例
* 简单任务控制: ./threadtask init.php
* 等待任务完成: ./threadtask sem.php
* 共享变量: ./threadtask var.php [threads [seconds [type]]]
* ini配置加载: ./threadtask ini.php demo.ini
  * SIGUSR1: 重启进程(restart)
  * SIGUSR2: 重载配置(reload)
* 过期共享变量: ./threadtask expire.php
* TCP服务: ./threadtask socket.php
* HTTP服务: ./threadtask http-server.php

