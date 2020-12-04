<?php
$running = true;
$exitSig = 0;

function signal($sig) {
	global $running, $exitSig;

	$running = false;
	$exitSig = $sig;
	task_set_run(false);
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);

if(empty($_SERVER['argv'][1])) exit("usage: {$_SERVER['_']} {$_SERVER['argv'][0]} <inifile> [isdebug [timefile [lockfile]]]\n");

define('FILE', $_SERVER['argv'][1]);
define('DEBUG', $_SERVER['argv'][2]??0);
define('TIME', $_SERVER['argv'][3]??FILE.'.time');
define('LOCK', $_SERVER['argv'][4]??FILE.'.lock');
define('KEYS', ['Y', 'm', 'd', 'w', 'H', 'i', 's']);

is_file(FILE) or die("ini file not exists\n");

is_file(LOCK) and die("running...\n");
touch(LOCK);

ini_set('memory_limit', -1);

$time = microtime(true);

share_var_init();

$T = 0;
$crons = [];
$times = (($t = @file_get_contents(TIME))?(json_decode($t, true)?:[]):[]);
$time = microtime(true);
while($running) {
	if($T != ($t = filemtime(FILE))) {
		$T = $t;
		
		task_wait(SIGINT);
		task_set_run($running);

		if(!$running) break;

		$cfgs = parse_ini_file(FILE, true, INI_SCANNER_RAW);
		if($cfgs === false) {
			echo error_get_last()['message'];
			break;
		}

		cfg_vars($cfgs);

		foreach($cfgs as $key => &$cfg) {
			if(!isset($cfg['type'])) {
				echo "No type parameter at key '$key'\n";
				unset($cfgs[$key]);
				$running = false;
				continue;
			}
			
			$cfg['type'] = strtolower($cfg['type']);
			
			switch($cfg['type']) {
				case 'php':
				case 'once':
				case 'script':
				case 'daemon':
					if($cfg['type'] === 'once') $n = 1;
					else $n = max(1, $cfg['count'] ?? 1);
					
					for($i=0; $i<$n; $i++) {
						if(!DEBUG && !create_task($key, $cfg['file'], $cfg['args']??[], $cfg['logfile']??null, $cfg['logmode']??'ab')) {
							echo "create task $key failure\n";
						}
					}
					unset($cfgs[$key]);
				case 'cron':
					if(!isset($cfg['file'])) {
						echo "No file parameter at key '$key'\n";
						unset($cfgs[$key]);
						$running = false;
					}
					if($cfg['type'] === 'cron' && !isset($cfg['cron'])) {
						echo "No cron parameter at key '$key'\n";
						unset($cfgs[$key]);
						$running = false;
					}
					break;
				default:
					echo "unknown type '{$cfg['type']}' at key '$key'\n";
					unset($cfgs[$key]);
					$running = false;
					break;
			}
		}
		unset($cfg);
		
		if(!$running) break;
	}

	isset($ctime) and usleep(1000000 - 1000000 * (microtime(true) - $time));
	
	$time = microtime(true);
	$ctime = (int) $time;
	$a1 = array_combine(KEYS, explode(' ', date('Y m d w H i s', $ctime)));
	
	if(DEBUG) {
		$isHas = false;
		
		$t = implode(' ', $a1);
		echo "a1: $t\n";
	}

	foreach($cfgs as $key => &$cfg) {
		if(isset($crons[$key])) {
			if(task_is_run($crons[$key])) continue;
			else unset($crons[$key]);
		}
		
		if(!isset($times[$key])) {
			$times[$key] = $ctime;
			continue;
		}

		$a0 = preg_split('/\s/', '* ' . $cfg['cron']);
		if(count($a0) != count(KEYS)) {
			echo "Unknown cron '{$cfg['cron']}' parameter size at key '$key'\n";
			continue;
		}

		$mtime = $times[$key];
		
		$a0 = array_combine(KEYS, $a0);
		$a2 = array_combine(KEYS, explode(' ', date('Y m d w H i s', $mtime)));

		$isNext = true;
		foreach($a0 as $k=>$v) {
			if($v === '*') continue;
			
			if(!preg_match('/^(\*\/)?\d+$/', $v)) {
				echo "Error cron parameter '$k' => '$v' at key '$key'\n";
				continue;
			}
			
			if(!strncmp($v, '*/', 2)) {
				$v = max(1, (int) substr($v, 2));

				switch($k) {
					case 'm':
						$t = ($a1['Y'] - $a2['Y']) * 12 + $a1['m'] - $a2['m'];
						break;
					case 'd':
						$t = ($ctime - $mtime) / 86400;
						break;
					case 'w':
						$t = ($ctime - $mtime) / (7 * 86400);
						break;
					case 'H':
						$t = ($ctime - $mtime) / 3600;
						break;
					case 'i':
						$t = ($ctime - $mtime) / 60;
						break;
					case 's':
						$t = $ctime - $mtime;
						break;
					default:
						$t = 0;
						break;
				}
				
				if($t < $v) {
					$isNext = false;
					break;
				}
			} else {
				if($a0[$k] !== $a1[$k] || $a2[$k] === $a1[$k]) {
					$isNext = false;
					break;
				}
			}
		}

		if($isNext) {
			if(DEBUG) {
				$isHas = true;
				
				echo "-------------------------\n$key\n";
				
				$t = implode(' ', $a0);
				echo "a0: $t\n";
				
				$t = implode(' ', $a2);
				echo "a2: $t\n";
			}

			if(!DEBUG && !create_task($key, $cfg['file'], $cfg['args']??[], $cfg['logfile']??null, $cfg['logmode']??'ab', $crons[$key])) {
				echo "create task $key failure\n";
			} else {
				$times[$key] = $ctime;
			}
		}
	}
	unset($cfg);
	
	if(DEBUG && $isHas) echo "-------------------------\n";
}

task_wait($exitSig?:SIGINT);

$crons = null;

share_var_destory();

file_put_contents(TIME, json_encode($times, JSON_PRETTY_PRINT|JSON_UNESCAPED_UNICODE));

unlink(LOCK);

function cfg_vars(array &$vars) {
	foreach($vars as &$var) {
		if(is_array($var)) cfg_vars($var);
		else $var = preg_replace_callback('/\$\{?([a-z_][0-9a-z_]*)\}?/i', function($matches) {
			return getenv($matches[1]);
		}, $var);

	}
}
