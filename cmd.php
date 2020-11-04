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

sleep(mt_rand(1, 5));

