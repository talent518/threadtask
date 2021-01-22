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
	
	$fd = socket_import_fd((int) $_SERVER['argv'][1]);
	if(strncmp(THREAD_TASK_NAME, 'read', 4) === 0) {
		while($running) {
			if(($n = @socket_read($fd, 8)) === false || strlen($n) !== 8) continue;
			
			share_var_inc('read', 1);

			$i = unpack('q', $n)[1];
			share_var_get_and_del('data', $i) or printf("DEL: $i\n");
		}
	} else {
		while($running) {
			$i = share_var_inc('write', 1);
			share_var_set('data', $i, 1) or printf("SET: $i\n");
			//share_var_get('data', $i) or printf("GET: $i\n");
			@socket_write($fd, pack('q', $i));
		}
	}
	
	socket_export_fd($fd, true); // skip close socket
	
	//echo THREAD_TASK_NAME . " Closed\n";
} else {
	socket_create_pair(AF_UNIX, SOCK_STREAM, 0, $pairs) or strerror('socket_set_option');
	$rfd = socket_export_fd($pairs[0]);
	$wfd = socket_export_fd($pairs[1]);

	share_var_init(3);
	for($i=0; $i<40; $i++) create_task('read' . $i, __FILE__, [$rfd]);
	for($i=0; $i<10; $i++) create_task('write' . $i, __FILE__, [$wfd]);
	
	$i = 0;
	while($running) usleep(10000);
	
	task_wait($exitSig?:SIGINT);
	
	var_dump(share_var_get());
	
	foreach($pairs as &$fd) {
		@socket_shutdown($fd) or strerror('socket_shutdown', false);
		@socket_close($fd);
	}
	unset($fd);
	
	share_var_destory();
	
	echo "Stoped\n";
}

