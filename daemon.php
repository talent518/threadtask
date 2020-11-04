#!/usr/bin/env php
<?php
$running = true;

function signal($sig) {
	global $running;
	echo 'daemon sig = ', $sig, PHP_EOL;
	$running = false;
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);

var_dump($_SERVER['argv']);

while($running) usleep(10000);

var_dump($_SERVER['argv']);

