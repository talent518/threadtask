# threadtask
php多线程任务

### 编译并运行实例代码
* php线程安全模式的编译配置为 --enable-maintainer-zts --with-tsrm-pthreads
* 运行测试脚本：./threadtask init.php
* php.ini配置中opcache.protect_memory=0，如果设置为1会导航段错误，原因：这个参数是非线程安全的开关
* 重启信号SIGUSR1,SIGUSR2: kill -SIGUSR1 pid 或者在init.php中使用task_wait(SIGUSR1)

