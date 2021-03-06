<?php
var_dump($_SERVER['argv']);

$running = true;
$exitSig = 0;

function signal($sig) {
	global $running, $exitSig;
	echo 'init sig = ', $sig, PHP_EOL;
	$running = false;
	$exitSig = $sig;
	task_set_run(false);
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);
pcntl_signal(SIGUSR1, 'signal', false);
pcntl_signal(SIGUSR2, 'signal', false);

task_set_delay(3);
task_set_threads(4);

create_task('daemon1', __DIR__ . '/daemon.php', ['daemon1', rand(), bin2hex(random_bytes(1))], __DIR__ . '/daemon.log') or die('create_task failure');

create_task('daemon2', __DIR__ . '/daemon.php', ['daemon2', rand(), bin2hex(random_bytes(2))], __DIR__ . '/daemon.log') or die('create_task failure');

create_task('daemon3', __DIR__ . '/daemon.php', ['daemon3', rand(), bin2hex(random_bytes(3))], __DIR__ . '/daemon.log') or die('create_task failure');

create_task('cmd1', __DIR__ . '/cmd.php', ['cmd1', rand(), bin2hex(random_bytes(3))], __DIR__ . '/cmd.log') or die('create_task failure');

create_task('cmd2', __DIR__ . '/cmd.php', ['cmd2', rand(), bin2hex(random_bytes(3))], __DIR__ . '/cmd.log') or die('create_task failure');

while($running) usleep(10000);

task_wait($exitSig);

