<?php
/**
 * threadtask函数库
 *
 * git仓库地址:
 *    https://github.com/talent518/threadtask
 *    https://gitee.com/talent518/threadtask
 */

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////// 任务相关 /////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

/**
 * 主线程为main, 其他线程是create_task函数的$taskname参数值
 * 
 * @var string
 */
define('THREAD_TASK_NAME', 'main');

/**
 * 创建任务/线程
 *
 * @param string $taskname 任务名称，并且会在目标线程创建一个THREAD_TASK_NAME常量
 * @param string $filename php文件的完整/相对路径，相当于php命令中的file
 * @param array $params cli参数，与php filename arg1 arg2 arg3 ...命令中的参数[arg1,arg2,arg3...]类似
 * @param string $logfile 输出写入到日志文件
 * @param string $logmode 打开文件的模式
 * @param resource $res 用于等待任务完成
 *
 * @return bool
 */
function create_task(string $taskname, string $filename, array $params, string $logfile = '', string $logmode = 'ab', &$res = null) {}

/**
 * 任务是否已完成函数
 *
 * @param resource $res 由create_task的第6个引用传递的参数$res而来
 *
 * @return bool
 */
function task_is_run($res) {}

/**
 * 等待任务完成函数
 *
 * @param resource $res 由create_task的第6个引用传递的参数$res而来
 *
 * @return bool
 */
function task_join($res) {}

/**
 * 向指定任务发送$sig信号
 *
 * @param resource $res 由create_task的第6个引用传递的参数$res而来
 * @param int $sig 线程信号
 *
 * @return bool
 */
function task_kill($res, int $sig = SIGINT) {}

/**
 * 向所有线程发送$signal信号，并等待所有线程结束(只能在主线程中使用)
 *
 * @param int $signal 杀死线程的信号，如: SIGINT,SIGTERM,SIGUSR1,SIGUSR2
 *
 * @return bool
 */
function task_wait(int $signal) {}

/**
 * php运行结束或异常中断自启延时秒数
 *
 * @return int
 */
function task_get_delay() {}

/**
 * php异常运行时等待$delay秒后自动重试任务(默认值为1)
 *
 * @param int $delay 秒数
 *
 * @return int 返回旧值: 秒数
 */
function task_set_delay(int $delay) {}

/**
 * 获取线程/任务数
 *
 * @param bool $is_max 如果为true则返回最大线程数，否则返回当前线程/任务数
 *
 * @return int 线程/任务数
 */
function task_get_num(bool $is_max = false) {}

/**
 * 获取线程/任务数: 是task_get_num函数的别名
 *
 * @param bool $is_max 如果为true则返回最大线程数，否则返回当前线程/任务数
 *
 * @return int 线程/任务数
 */
function task_get_threads(bool $is_max = false) {}

/**
 * 设置最大线程数限制(默认值为256)
 *
 * @param int $threads 最大线程/任务数
 *
 * @return int 返回旧最大线程/任务数
 */
function task_set_threads(int $threads) {}

/**
 * 是否开启调试信息
 *
 * @return bool
 */
function task_get_debug() {}

/**
 * 设置调试信息开关(默认值为false)
 *
 * @param bool $isDebug
 *
 * @return bool 返回旧值
 */
function task_set_debug(bool $isDebug) {}

/**
 * 是否正在运行
 *
 * @return bool
 */
function task_get_run() {}

/**
 * 设置运行状态(默认值为true)
 *
 * 当运行状态为true时，有以下两种情况:
 *   1. php异常退出会自动尝试，
 *   2. 任务正常结束后新的任务会再次利用空闲线程(空闲时间可通过task_set_delay设置)
 *
 * @param bool $isRun 是否继续运行
 *
 * @return bool 返回旧值
 */
function task_set_run(bool $isRun) {}

/**
 * 设置线程信号掩码，默认阻塞所有信号
 *
 * @param int $how 包括以下可选值:
 *                   SIG_BLOCK: 把信号加入到当前阻塞信号中。
 *                   SIG_UNBLOCK: 从当前阻塞信号中移出信号。
 *                   SIG_SETMASK: 用给定的信号列表替换当前阻塞信号列表。
 * @param array $newset 新阻塞信号列表数组
 * @param array $oldset 是一个输出参数，用来返回之前的阻塞信号列表数组。
 *
 * @return bool
 */
function pthread_sigmask(int $how, array $newset, ?array &$oldset = null) {}

/**
 * 让出当前线程剩余的CPU时间片
 *
 * @return bool
 */
function pthread_yield(): bool {}

/**
 * 是否是主线程: 一般用于判断task_wait是否可用
 * 
 * @return bool
 */
function is_main_task(): bool {}

/**
 * is_main_task的别名
 * 
 * @return bool
 */
function is_main_thread(): bool {}

//////////////////////////////////////////////////////////////////////////////
////////////////////////////// 共享变量相关 ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

/**
 * 初始化共享变量(只能在主线程中使用)
 *
 * @param int $size 初始化变量数
 *
 * @return bool
 */
function share_var_init(int $size = 128) {}

/**
 * 回收共享变量(只能在主线程中使用)
 *
 * @param int $size 初始化变量数
 *
 * @return bool
 */
function share_var_destory() {}

/**
 * 是否存在指定的共享变量
 *   至少一个参数(键): 整数或字符串
 *
 * @param array ...$keys 是数组级联键，例如: share_var_exists($key1, $key2, $key3) 等效于 isset($share[$key1][$key2][$key3])
 *
 * @return bool
 */
function share_var_exists(...$keys) {}

/**
 * 读取共享变量
 *   无参数时返回所有变量数据
 *
 * @param array ...$keys 是数组级联键，例如: share_var_get($key1, $key2, $key3) 等效于 $share[$key1][$key2][$key3]
 *
 * @return mixed
 */
function share_var_get(...$keys) {}

/**
 * 读取并删除共享变量
 *   无参数时返回所有变量数据
 *
 * @param array ...$keys 是数组级联键，例如: share_var_get($key1, $key2, $key3) 等效于 $share[$key1][$key2][$key3]
 *
 * @return mixed
 */
function share_var_get_and_del(...$keys) {}

/**
 * 写入共享变量(至少一个参数，每个参数代表要查询的多维数组的key，最后一个是数组可与存在数组合并，否则则替换)，例如:
 *   share_var_put($key1, $key2, $key3, $value) 等效于 $share[$key1][$key2][$key3] = $value
 *   share_var_put(['a'=>1,'b'=>2]) 等效于 $share['a'] = 1;$share['b'] = 2
 *   share_var_put('a') 等效于 $share[]='a'
 *
 * @param array ...$keys 是数组级联键
 * @param mixed $value
 *
 * @return mixed
 */
function share_var_put($keys, $value) {}

/**
 * 累加共享变量($key[,...]查到的变量: 是数组则会把$value附加到数组后，是字符串则在其后附加$value字符串，其它数值类型或布尔值则会按数值累加)
 *
 * @param array ...$keys 是数组级联键
 * @param mixed $value
 * 
 * @return mixed 返回运算结果
 */
function share_var_inc($keys, $value) {}

/**
 * 写入共享变量(至少两个参数: $key, $value)，例如:
 *   share_var_set($key1, $key2, $key3, $value) 等效于 $share[$key1][$key2][$key3] = $value
 *   share_var_set(1, 'value') 等效于 $share[1] = 'value'
 *
 * @param array ...$keys 是数组级联键
 * @param mixed $value
 *
 * @return bool
 */
function share_var_set($keys, $value) {}

/**
 * 写入过期共享变量(至少三个参数: $key, $value, $expire)，例如:
 *   share_var_set_ex($key1, $key2, $key3, $value, 0) 等效于 $share[$key1][$key2][$key3] = $value
 *   share_var_set_ex(1, 'value', 0) 等效于 $share[1] = 'value'
 *
 * @param array ...$keys 是数组级联键
 * @param mixed $value
 * @param int $expire 过期时间戳，为0时永不过期
 *
 * @return bool
 */
function share_var_set_ex($keys, $value, int $expire) {}

/**
 * 删除共享变量
 *   至少一个参数(键): 整数或字符串
 *
 * @param array ...$keys 是数组级联键，例如: share_var_del($key1, $key2, $key3) 等效于 unset($share[$key1][$key2][$key3])
 *
 * @return bool
 */
function share_var_del(...$keys) {}

/**
 * 清空共享变量
 *
 * @return int 删除的变量个数
 */
function share_var_clean() {}

/**
 * 清理已过期共享变量
 *
 * @param int $expire 递归删除过期时为非0且小于$expire的变量
 *
 * @return int 删除的变量个数
 */
function share_var_clean_ex(int $expire) {}

/**
 * 统计变量(返回: 大于0为数组元素数，小于0为字符长度，true为对象，未找到为null，否则为false)
 *
 * @param array ...$keys 是数组级联键
 *
 * @return int|bool|null
 */
function share_var_count(...$keys) {}

//////////////////////////////////////////////////////////////////////////////
/////////////////////////// 线程安全的共享变量 //////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

/**
 * 声明线程安全的共享变量
 *
 * @param string|int|null $varname 变量名，为空则引用$var
 * @param resource|null $var 如果为空，则为根变量
 * @param bool $is_fd 如果为true，则可以使用ts_var_fd()函数
 *
 * @return resource|bool
 */
function ts_var_declare($varname, $var = null, bool $is_fd = false) {}

/**
 * 导出socket文件描述符的管道对(可使用sockets扩展中的函数进行操作)
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param bool $is_write 是否是写通道
 *
 * @return \Socket|resource|bool 为false时$var参数无效
 */
function ts_var_fd($var, bool $is_write = false) {}

/**
 * 设置共享变量的过期时间
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int $expire 过期时间戳，为0时永不过期
 *
 * @return bool|null 为false时$var参数无效或ts_var_declare时is_fd参数不是true
 */
function ts_var_expire($var, int $expire) {}

/**
 * 向线程安全变量中存储数据
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string $key 键名，可为字符串、整形或空，为空时把$val附加到最后
 * @param mixed $val 值
 * @param int $expire 过期时间戳，为0时永不过期
 *
 * @return bool 是否设置成功
 */
function ts_var_set($var, $key, $val, int $expire = 0): bool {}

/**
 * 向线程安全变量中存储数据: 是ts_var_set别名
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string $key 键名，可为字符串、整形或空，为空时把$val附加到最后
 * @param mixed $val 值
 * @param int $expire 过期时间戳，为0时永不过期
 *
 * @return bool 是否设置成功
 */
function ts_var_put($var, $key, $val, int $expire = 0): bool {}

/**
 * 压入队列
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param array ...$vals 多个值
 *
 * @return bool
 */
function ts_var_push($var, ...$vals): bool {}

/**
 * 弹出队列（线程安全变量）中最后一个
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string &$key 键名，弹出的值对应的键
 *
 * @return mixed 弹出的值
 */
function ts_var_pop($var, &$key = null) {}

/**
 * 弹出队列（线程安全变量）中第一个
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string &$key 键名，弹出的值对应的键
 *
 * @return mixed 弹出的值
 */
function ts_var_shift($var, &$key = null) {}

/**
 * 获取最小或最大键/值
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param bool $is_max
 * @param bool $is_key
 * @param int|string &$key 键名，最小或最大的值对应的键
 *
 * @return mixed 最小或最大的值
 */
function ts_var_minmax($var, bool $is_max = false, bool $is_key = false, &$key = null) {}

/**
 * 获取线程安全变量数据
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string|null $key 键名，可为字符串、整形或空，为空时返回$var中的所有变量
 * @param bool $is_del 是否删除该变量
 *
 * @return mixed
 */
function ts_var_get($var, $key = null, bool $is_del = false) {}

/**
 * 获取线程安全变量数据
 *   注意1: 使用CFLAGS=-DLOCK_TIMEOUT=1 make进行编译可以调试死锁，性能有些差
 *   注意2: 可能会导致死锁现象，请慎用
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string $key 键名
 * @param callable $callback $key不存在时调用$callback并把返回值设置到与$key对应的值中
 * @param bool $expire 过期时间戳，为0时永不过期
 * @param array ...$parameters 0个或以上的参数，被传入回调函数$callback
 *
 * @return mixed 返回已存在的值或$callback函数的返回值
 */
function ts_var_get_or_set($var, $key, callable $callback, int $expire = 0, ...$parameters) {}

/**
 * 线程安全的加写锁或加读锁
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string $key 键名
 * @param int $expire 过期时间戳，为0时永不过期，大于0时自动加time()
 * @param bool $isWrite 是否为写锁
 * @return bool
 */
function ts_var_lock($var, $key, int $expire, bool $isWrite): bool {}

/**
 * 线程安全的解写锁或解读锁
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string $key 键名
 * @param bool $isWrite 是否为写锁
 *
 * @return bool
 */
function ts_var_unlock($var, $key, bool $isWrite): bool {}

/**
 * 删除线程安全变量中的数据
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string $key 键名
 *
 * @return bool
 */
function ts_var_del($var, $key): bool {}

/**
 * 自增线程安全变量并返回
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int|string|null $key 键名，为空时把$inc附加到$var最后
 * @param mixed $inc 为数值类型则累加，为字符串则附加到已存在字符串末尾，已存在为数组则把$inc附加其后，
 *
 * @return mixed
 */
function ts_var_inc($var, $key, $inc) {}

/**
 * 获取线程安全变量有多少个数据（与count函数类似）
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 *
 * @return int
 */
function ts_var_count($var) {}

/**
 * 清理线程安全变量并返回元素个数
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param int $expire 为0时返回清空前元素个数，否则返回清理已过期后剩余的元素个数
 *
 * @return int
 */
function ts_var_clean($var, int $expire = 0) {}

/**
 * 重建线程安全变量索引
 *
 * @param resource $var 由ts_var_declare函数返回的变量
 * @param bool $only_integer_keys 是否紧重新排序整数键
 *
 * @return bool
 */
function ts_var_reindex($var, bool $only_integer_keys = false): bool {}

/**
 * 获取线程安全变量所有键名
 * 
 * @param resource $var 由ts_var_declare函数返回的变量
 * 
 * @return bool|array
 */
function ts_var_keys($var) {}

/**
 * 获取线程安全变量所有键过期时间
 * 
 * @param resource $var 由ts_var_declare函数返回的变量
 * 
 * @return bool|array
 */
function ts_var_expires($var) {}

//////////////////////////////////////////////////////////////////////////////
////////////////////////////// sockets附加函数 ////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

/**
 * 导出socket文件描述符为整型
 *
 * @param \Socket|resource $socket 通过socket_create或socket_create_pair或ts_var_fd创建的socket套接字或socket管道
 * @param bool $is_close 为true时，不自动关闭$socket文件描述符，使用socket_close($socket)也一样
 *
 * @return int|bool 返回整数类型的文件描述符(unix平台)
 */
function socket_export_fd($socket, bool $is_close = false): int {}

/**
 * 从整型导入socket资源类型值
 *
 * @param int $fd 来自socket_export_fd的返回的整型值
 *
 * @return \Socket|resource|bool
 */
function socket_import_fd(int $fd) {}

/**
 * 接受socket连接
 *
 * @param int $sockfd 来自socket_export_fd的返回的整型值
 * @param string &$addr 客户端IP地址
 * @param int &$port 客户端端口号
 *
 * @return \Socket|resource|bool
 */
function socket_accept_ex(int $sockfd, string &$addr, int &$port) {}

//////////////////////////////////////////////////////////////////////////////
////////////////////////////// mysqli附加函数 /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

/**
 * 导出mysqli文件描述符为整型
 *
 * @param mysqli $mysql
 *
 * @return int|bool 返回整数类型的文件描述符(unix平台)
 */
function mysqli_export_fd(mysqli $mysql) {}

/**
 * 异步执行mysqli预处理SQL
 *
 * @param mysqli_stmt $stmt
 *
 * @return bool
 */
function mysqli_stmt_async_execute(mysqli_stmt $stmt): bool {}

/**
 * 获取mysqli预处理SQL的执行结果
 *
 * @return bool
 */
function mysqli_stmt_reap_async_query(mysqli_stmt $stmt): bool {}

//////////////////////////////////////////////////////////////////////////////
////////////////////////////// 代码块执行超时处理 ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////

/**
 * 设置超时
 *
 * @param int $seconds
 *
 * @return bool
 */
function set_timeout(int $seconds = 1): bool {}

/**
 * 清除超时
 *
 * @return bool
 */
function clear_timeout(): bool {}

/**
 * 触发超时
 *
 * @param int $signal 向任务线程发送的信号，使用 pcntrl_signal()函数绑定信号处理函数
 *
 * @return bool
 */
function trigger_timeout(int $signal = SIGALRM): bool {}

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// PHP行为函数 /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

/**
 * 执行一个函数，如果$call函数执行时遇到die函数和exit语句的时会抛出一个GoExitException异常
 *
 * @param callable $call
 * @param array ...$args $call函数的参数
 *
 * @return mixed $call函数的执行结果
 */
function go(callable $call, ...$args) {}

/**
 * 调用并清除由register_shutdown_function注册的php中止函数
 *
 * @return void
 */
function call_and_free_shutdown() {}

/**
 * 重新定义常量
 *
 * @param string $name
 * @param mixed $value
 * @param bool $case_insensitive
 *
 * @return bool
 */
function redefine(string $name, $value, bool $case_insensitive = false): bool {}

/**
 * go的$call函数执行时遇到die函数或exit语句时抛出
 */
class GoExitException extends Exception {
	/**
	 * exit code
	 * 
	 * @return int
	 */
	public function getStatus(): int {}
}

//////////////////////////////////////////////////////////////////////////////
///////////////////////////////// 其它扩展函数 /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

/**
 * 获取文件系统信息
 *
 * @param string $path 文件或文件夹路径
 * 
 * @return bool|array 为数组时，每个键的意义：total(总字节数)，avail(可用字节数)，free(空闲字节数)，ftotal(总文件节点数)，avail(可用文件节点数)，free(空闲文件节点数)，namemax(文件名最大长度)
 */
function statfs(string $path) {}

/**
 * statfs的别名
 *
 * @param string $path
 * 
 * @return bool|array
 */
function statvfs(string $path) {}
