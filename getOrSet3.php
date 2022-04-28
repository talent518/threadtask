<?php
pthread_sigmask(SIG_SETMASK, []);
	
$running = true;

pcntl_async_signals(true);
pcntl_signal(SIGINT, function() use(&$running) {
	printf("SIGINT\n");
	$running = false;
}, false);

$var1 = ts_var_declare(1);
$var2 = ts_var_declare(2);

register_shutdown_function(function() {
	printf("EXIT: %s\n", THREAD_TASK_NAME);
});

if(!is_main_task()) {
	$i = 0;
	if(THREAD_TASK_NAME == 'task0') {
		while($running) {
			ts_var_get_or_set($var1, ++$i, function() use($var2, &$i) {
				return ts_var_get_or_set($var2, ++$i, function() use($i) {
					usleep(5000); // 5ms
					return $i;
				});
			});
			if($i >= 10000) {
				$i = 0;
				echo "OK\n";
			}
		}
	} else {
		while($running) {
			ts_var_get_or_set($var2, ++$i, function() use($var1, &$i) {
				ts_var_get_or_set($var1, ++$i, function() {
					usleep(5000); // 5ms
					return 1;
				});
			});
			if($i >= 10000) {
				$i = 0;
				echo "OK\n";
			}
		}
	}
} else {
	create_task('task0', __FILE__, [], null, null, $waits[0]);
	create_task('task1', __FILE__, [], null, null, $waits[1]);
	
	$var = ts_var_declare(null);
	
	while($running && ($n = task_get_threads()) > 0) {
		sleep(1);
		echo "$n\n";
	}
	
	task_wait(SIGINT);
}

