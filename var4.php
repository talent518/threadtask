<?php
$running = true;
$exitSig = 0;

function signal($sig) {
        global $running, $exitSig;
 
        $running = false;
        $exitSig = $sig;
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);

define('SUCCESS', 0);
define('FAILURE', 1);

$vars = ts_var_declare(null);
$stat = ts_var_declare('stat');
$var = ts_var_declare('var');
$time = time();

if(defined('THREAD_TASK_NAME')) {
	while($running) {
		switch(rand(0, 15)) {
			case 0:
				$fd = ts_var_declare('fd', $var, true);
				$r = ts_var_fd($fd);
				$w = ts_var_fd($fd, true);
				if(!@socket_write($w, 'a') || !@socket_read($r, 1)) {
					ts_var_inc($stat, FAILURE, 1);
				}
				socket_export_fd($r, true);
				socket_export_fd($w, true);
				unset($fd, $r, $w);
				break;
			case 1:
				ts_var_del($var, 'fd') or ts_var_inc($stat, FAILURE, 1);
				break;
			case 2:
				$res = ts_var_declare('declare.del', $var);
				ts_var_push($res, 'L', 'C', 'R');
				ts_var_pop($res, $i);
				ts_var_shift($res, $i);
				ts_var_pop($res, $i);
				break;
			case 3:
				ts_var_del($var, 'declare.del') or ts_var_inc($stat, FAILURE, 1);
				break;
			case 4:
				ts_var_set($var, 'set.del', rand()) or ts_var_inc($stat, FAILURE, 1);
				break;
			case 5:
				ts_var_del($var, 'set.del') or ts_var_inc($stat, FAILURE, 1);
				break;
			case 6:
				ts_var_get($var, 'set.del', true) or ts_var_inc($stat, FAILURE, 1);
				break;
			case 7:
				ts_var_set($var, 'expire', random_bytes(16), $time + 1) or ts_var_inc($stat, FAILURE, 1);
				break;
			case 8:
				ts_var_exists($var, 'expire') or ($time = time() + 1);
				break;
			case 9:
				ts_var_count($var);
				break;
			case 10:
			case 11:
			case 12:
			case 13:
				ts_var_inc($var, rand(0, 99), 1);
				break;
			case 14:
				ts_var_del($var, rand(0, 99)) or ts_var_inc($stat, FAILURE, 1);
				break;
			case 15:
				$time % 10 === 0 and ts_var_reindex($var);
				break;
		}
		ts_var_inc($stat, SUCCESS, 1);
	}
} else {
	$threads = (int) ($_SERVER['argv'][1] ?? 4);

	for($i=0; $i<$threads; $i++) {
		create_task('task' . $i, __FILE__, []);
	}

	$stat = ts_var_declare('stat');

	while($running) {
		sleep(1);
		$n = ts_var_clean($vars, ++$time) + ts_var_count($var);
		$success = (int) ts_var_get($stat, SUCCESS, true);
		$failure = (int) ts_var_get($stat, FAILURE, true);
		echo "vars: $n, success: $success, failure: $failure\n";
	}

	task_wait($exitSig?:SIGINT);

	$n = ts_var_clean($vars);
	echo "vars: $n\n";
}
