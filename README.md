# threadtask
php多线程任务，优点是占用内存少且稳定，对于并行任务处理也有另活的应用。

### 编译并运行实例代码
* php线程安全模式的编译配置为 --enable-maintainer-zts --with-tsrm-pthreads
* 运行测试脚本：./threadtask init.php
* php.ini配置中opcache.protect_memory=0，如果设置为1会导航段错误，原因：这个参数是非线程安全的开关
* 重启信号SIGUSR1,SIGUSR2: kill -SIGUSR1 pid 或者在init.php中使用task_wait(SIGUSR1)

### 函数说明
* 创建任务: create_task($taskname, $filename, $params, $logfile = NULL, $logmode = 'ab')
  * $taskname: string 任务名称
  * $filename: string php文件的完整/相对路径，相当于php命令中的file
  * $params: array cli参数，与php filename arg1 arg2 arg3 ...命令中的参数[arg1,arg2,arg3...]类似
  * $logfile: string 输出写入到日志文件
  * $logmode: string 打开文件的模式
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

