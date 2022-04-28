<?php
$running = true;
$exitSig = 0;
function signal($sig) {
	global $running, $exitSig;

	$exitSig = $sig;
	$running = false;
	is_main_task() and task_set_run(false);
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);
pcntl_signal(SIGUSR1, 'signal', false);
pcntl_signal(SIGUSR2, 'signal', false);

if(!is_main_task()) {
	$host = $_SERVER['argv'][1];
	$port = (int) $_SERVER['argv'][2];
	while($running) {
		$fd = @socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
		if($fd === false) {
			share_var_inc('errs', 1);
			//strerror('socket_connect', false);
			continue;
		}
		@socket_set_option($fd, SOL_SOCKET, SO_LINGER, ['l_onoff'=>1, 'l_linger'=>1]);
		@socket_set_option($fd, SOL_SOCKET, SO_REUSEADDR, 1) or strerror('socket_set_option', false);
		if(!@socket_connect($fd, $host, $port)) {
			share_var_inc('errs', 1);
			//strerror('socket_connect', false);
			@socket_close($fd);
			continue;
		}
		if(($str = @socket_read($fd, 1024)) !== false) {
			share_var_inc('conns', 1);
			//echo $str;
			@socket_write($fd, '1');
		} else share_var_inc('errs', 1);
		@socket_close($fd);
	}
} else {
	$host = ($_SERVER['argv'][1]??'127.0.0.1');
	$port = (int) ($_SERVER['argv'][2]??5000);
	$conns = (int) ($_SERVER['argv'][3]??100);
	
	task_set_threads($conns);
	share_var_init(2);
	for($i=0; $i<$conns; $i++) create_task('conn' . $i, __FILE__, [$host, $port]);
	$n = 0;
	$e = 0;
	while($running) {
		sleep(1);
		$n2 = share_var_get('conns');
		$n3 = share_var_get('errs');
		$n = $n2 - $n;
		$e = $n3 - $e;
		echo "$n connects, $e errors\n";
		$n = $n2;
		$e = $n3;
	}
	
	task_wait($exitSig?:SIGINT);
	share_var_destory();
	
	echo "Stoped\n";
}
	
function strerror($msg, $isExit = true) {
	$err = socket_last_error();
	printf("[%s] %s(%d): %s\n", THREAD_TASK_NAME, $msg, $err, socket_strerror($err));

	if($isExit) exit; else return true;
}

