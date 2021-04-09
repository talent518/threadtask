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
$var3 = ts_var_declare(3);

register_shutdown_function(function() {
	printf("EXIT: %s\n", THREAD_TASK_NAME);
});

if(defined('THREAD_TASK_NAME')) {
	$i = 0;
	if(THREAD_TASK_NAME == 'task0') {
		while($running) {
			ts_var_get_or_set($var1, ++$i, function() use($var2, $var3, &$i) {
				return ts_var_get_or_set($var3, ++$i, function() use($var2, &$i) {
					usleep(5000); // 5ms
					return $i;
				});
			});
			if($i >= 10000) {
				$i = 0;
				echo "OK\n";
			}
		}
	} elseif(THREAD_TASK_NAME == 'task1') {
		while($running) {
			ts_var_get_or_set($var2, ++$i, function() use($var1, $var3, &$i) {
				ts_var_get_or_set($var1, ++$i, function() use($var3, &$i) {
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
			ts_var_get_or_set($var3, ++$i, function() use($var2, $var1, &$i) {
				ts_var_get_or_set($var2, ++$i, function() use($var1, &$i) {
					usleep(5000); // 5ms
					return $i;
				});
			});
			if($i >= 10000) {
				$i = 0;
				echo "OK\n";
			}
		}
	}
} else {
	define('THREAD_TASK_NAME', 'main');
	
	create_task('task0', __FILE__, [], null, null, $waits[0]);
	create_task('task1', __FILE__, [], null, null, $waits[1]);
	create_task('task2', __FILE__, [], null, null, $waits[2]);
	
	$var = ts_var_declare(null);
	
	while($running && ($n = task_get_threads()) > 0) {
		sleep(1);
		echo "$n\n";
	}
	
	task_wait(SIGINT);
}

