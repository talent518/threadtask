<?php
$running = true;
$exitSig = 0;

function signal($sig) {
	global $running, $exitSig;
	echo 'init sig = ', $sig, PHP_EOL;
	$running = false;
	$exitSig = $sig;
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);
pcntl_signal(SIGUSR1, 'signal', false);
pcntl_signal(SIGUSR2, 'signal', false);

$var = ts_var_declare(null);

$memsize = memory_get_usage();

if(defined('THREAD_TASK_NAME')) {
	while($running) {
		ts_var_shift($var, $key);
		echo "$key\n";
	};
} else {
	create_task('pop', __FILE__, []) or die('create_task failure');

	$i = 0;
	while($running) {
		ts_var_count($var) > 5 or ts_var_set($var, $i++, __FILE__);
	};

	task_wait($exitSig);
}

$memsize = memory_get_usage() - $memsize;

echo "memory: $memsize\n";

