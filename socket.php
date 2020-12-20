<?php
$running = true;
$exitSig = 0;
function signal($sig) {
	global $running;

	$exitSig = $sig;
	$running = false;
	defined('THREAD_TASK_NAME') or task_set_run(false);
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);

if(defined('THREAD_TASK_NAME')) {
	// echo THREAD_TASK_NAME . PHP_EOL;
	
	if(strncmp(THREAD_TASK_NAME, 'accept', 6) == 0) {
		$sock = socket_import_fd((int) $_SERVER['argv'][1]);
		$flag = (bool) $_SERVER['argv'][2];
		if(!$flag) $wfd = socket_import_fd((int) $_SERVER['argv'][3]);
		
		while($running && ($fd = @socket_accept($sock)) !== false) {
			$i = share_var_inc('conns', 1);
			$fd = socket_export_fd($fd, true);
			share_var_set('accepts', $i, $fd);
			if($flag) create_task('read' . $i, __FILE__, [$fd,$i]);
			else socket_write($wfd, pack('q', $i));
		}

		socket_export_fd($sock, true); // skip close socket
		if(!$flag) socket_export_fd($wfd, true); // skip close socket

		//echo THREAD_TASK_NAME . " Closed\n";
	} elseif(empty($_SERVER['argv'][1])) {
		$rfd = socket_import_fd((int) $_SERVER['argv'][2]);
		while($running) {
			if(($n = @socket_read($rfd, 8)) === false || strlen($n) !== 8) continue;
			
			$i = unpack('q', $n)[1];
			
			$fd = share_var_get_and_del('accepts', $i);

			$str = THREAD_TASK_NAME . " $fd\n";
			$fd = socket_import_fd($fd);
            @socket_set_option($fd, SOL_SOCKET, SO_LINGER, ['l_onoff'=>1, 'l_linger'=>0]) or strerror('socket_set_option', false);
			@socket_write($fd, $str) > 0 or strerror('socket_write', false);
			@socket_read($fd, 1);
			//@socket_shutdown($fd) or strerror('socket_shutdown', false);
			@socket_close($fd);
		}
		
		socket_export_fd($rfd, true); // skip close socket
		
		//echo THREAD_TASK_NAME . " Closed\n";
	} else {
		$fd = (int) $_SERVER['argv'][1];
		$i = (int) $_SERVER['argv'][2];
		share_var_del('accepts', $i);
		
		$str = THREAD_TASK_NAME . " $fd\n";
		$fd = socket_import_fd($fd);
        @socket_set_option($fd, SOL_SOCKET, SO_LINGER, ['l_onoff'=>1, 'l_linger'=>0]) or strerror('socket_set_option', false);
		@socket_write($fd, $str) > 0 or strerror('socket_write', false);
		@socket_read($fd, 1);
		@socket_close($fd);
	}
} else {
	$host = ($_SERVER['argv'][1]??'127.0.0.1');
	$port = ($_SERVER['argv'][2]??5000);
	$flag = ($_SERVER['argv'][3]??0);
	
	if($flag) {
		$rfd = $wfd = 0;
	} else {
		socket_create_pair(AF_UNIX, SOCK_STREAM, 0, $pairs) or strerror('socket_set_option');
		$rfd = socket_export_fd($pairs[0]);
		$wfd = socket_export_fd($pairs[1]);
	}

	($sock = @socket_create(AF_INET, SOCK_STREAM, SOL_TCP)) or strerror('socket_create');
	@socket_bind($sock, $host, (int)$port) or strerror('socket_bind');
	@socket_listen($sock, 128) or strerror('socket_listen');
	
	$fd = socket_export_fd($sock);

	echo "Server listening on $host:$port\n";

	share_var_init(3);
	if(!$flag) share_var_set('accepts', []);
	for($i=0; $i<8; $i++) create_task('accept' . $i, __FILE__, [$fd,$flag,$wfd]);
	if(!$flag) for($i=0; $i<56; $i++) create_task('read' . $i, __FILE__, [0,$rfd]);
	$n = 0;
	while($running) {
		sleep(1);
		$n2 = share_var_get('conns');
		$n3 = share_var_count('accepts');
		$n = $n2 - $n;
		echo "$n connects, $n3 accepts\n";
		$n = $n2;
		
		//var_dump(share_var_get());
	}
	
	task_wait($exitSig?:SIGINT);
	
	@socket_shutdown($sock) or strerror('socket_shutdown');
	@socket_close($sock);
	
	$accepts = share_var_get('accepts');
	
	foreach($accepts as $i=>$fd) {
		echo "unread: $i=>$fd\n";
		$fd = socket_import_fd($fd);
		@socket_shutdown($fd) or strerror('socket_shutdown', false);
		@socket_close($fd);
	}
	
	if(!$flag) {
		foreach($pairs as $fd) {
			@socket_shutdown($fd) or strerror('socket_shutdown', false);
			@socket_close($fd);
		}
	}
	
	share_var_destory();
	
	echo "Stoped\n";
}
	
function strerror($msg, $isExit = true) {
	$err = socket_last_error();
	printf("[%s] %s(%d): %s\n", defined('THREAD_TASK_NAME') ? THREAD_TASK_NAME : 'main', $msg, $err, socket_strerror($err));

	if($isExit) exit; else return true;
}

