#!/usr/bin/env php
<?php
$running = true;

function signal($sig) {
	global $running;
	echo 'cmd sig = ', $sig, PHP_EOL;
	$running = false;
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);

var_dump($_SERVER['argv']);

$t = microtime(true);
usleep(mt_rand(10000, 1000000));
echo 'runtime ', (int)((microtime(true)-$t)*1000), 'ms', PHP_EOL;

if(isset($_SERVER['argv'][1]) && $_SERVER['argv'][1] === 'return') {
	share_var_set($_SERVER['argv'][1], microtime(true) - $t);
}

