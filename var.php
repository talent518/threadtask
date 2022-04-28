<?php
$running = true;
$exitSig = 0;

function signal($sig) {
	global $running, $exitSig, $isThread;
	if($isThread) echo "sig = $sig\n"; else echo "init sig = $sig\n";
	$running = false;
	$exitSig = $sig;
	is_main_task() and task_set_run(false);
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);

if(!is_main_task()) {
	switch((int) $_SERVER['argv'][1]) {
		case 1: // put = get+1
			while($running) share_var_put(THREAD_TASK_NAME, share_var_get(THREAD_TASK_NAME) + 1);
			break;
		case 2: // str += 's'
			while($running) share_var_inc(THREAD_TASK_NAME, chr(mt_rand(0,95)+32));
			break;
		case 3: // arr[] = rand()
			share_var_set(THREAD_TASK_NAME, []);
			while($running) share_var_inc(THREAD_TASK_NAME, mt_rand(0,128));
			break;
		case 4:
			$n = THREAD_TASK_NAME;
			while($running) {
				$i = share_var_inc(THREAD_TASK_NAME, 1);
				echo "\033[31m$n:\033[0m $i\n";
			}
			break;
		case 5: // str += 's'
			$n = THREAD_TASK_NAME;
			while($running) {
				$s = share_var_inc(THREAD_TASK_NAME, chr(mt_rand(0,95)+32));
				$i = strlen($s);
				echo "\033[31m$n(\033[36m$i\033[31m):\033[0m $s\n";
			}
			break;
		case 6: // arr[] = rand()
			$n = THREAD_TASK_NAME;
			share_var_set(THREAD_TASK_NAME, []);
			while($running) {
				$a = share_var_inc(THREAD_TASK_NAME, mt_rand(0,128));
				echo "\033[31m$n:\033[0m $a\n";
			}
			break;
		case 7: // arr[] = new stdClass
			$n = THREAD_TASK_NAME;
			share_var_set(THREAD_TASK_NAME, []);
			$o = new stdClass;
			$o->a = 0;
			while($running) {
				$a = share_var_inc(THREAD_TASK_NAME, $o);
				$o->a = $a;
				echo "\033[31m$n:\033[0m $a\n";
			}
			break;
		default:
			while($running) share_var_inc(THREAD_TASK_NAME, 1);
			break;
	}
	exit;
}

// echo "usage: {$_SERVER['_']} {$_SERVER['argv'][0]} [threads [seconds [type]]]\n";

define('TYPE', (int) ($_SERVER['argv'][3]??0));

ini_set('memory_limit', -1);

$time = microtime(true);

share_var_init();

$n = ($_SERVER['argv'][1]??4);
for($i=0; $i<$n; $i++) create_task('var' . $i, __FILE__, [TYPE]);

sleep($_SERVER['argv'][2]??10);

task_wait($exitSig?:SIGINT);

$vars = share_var_get();

$nn = share_var_destory();

$time = microtime(true) - $time;

switch(TYPE) {
	case 2:
	case 5:
		// var_dump($vars);
		$n = array_sum(array_map(function($s) {return strlen($s);},$vars));
		break;
	case 3:
	case 6:
	case 7:
		// var_dump($vars);
		$n = array_sum(array_map(function($a) {return count($a);},$vars));
		break;
	default:
		// var_dump($vars);
		$n = array_sum($vars);
		break;
}

$mem = memory_get_usage(true);
unset($vars);
$mem = $mem - memory_get_usage(true);

echo $nn, ' vars', PHP_EOL;
echo "var read and write $n times at $time seconds\n";
echo "$mem bytes\n";

