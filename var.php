<?php
$running = true;
$exitSig = 0;
$isThread = (count($_SERVER['argv']) === 2 && $_SERVER['argv'][1] === 'thread');

function signal($sig) {
	global $running, $exitSig, $isThread;
	if($isThread) echo "sig = $sig\n"; else echo "init sig = $sig\n";
	$running = false;
	$exitSig = $sig;
	task_set_run(false);
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);

if($isThread) {
	while($running) share_var_put(THREAD_TASK_NAME, share_var_get(THREAD_TASK_NAME) + 1);
	exit;
}

$time = microtime(true);

share_var_init();

$n = ($_SERVER['argv'][1]??4);
for($i=0; $i<$n; $i++) create_task('var' . $i, __FILE__, ['thread']);

sleep($_SERVER['argv'][2]??10);

task_wait($exitSig?:SIGINT);

$vars = share_var_get();

var_dump($vars);
$n = array_sum($vars);

echo share_var_destory(), ' vars', PHP_EOL;

$time = microtime(true) - $time;

echo "var read and write $n times at $time seconds\n";

