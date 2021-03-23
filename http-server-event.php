<?php
$running = true;
$exitSig = 0;
function signal(int $sig) {
	global $running, $exitSig;

	$exitSig = $sig;
	$running = false;
	
	if(defined('THREAD_TASK_NAME')) {
		// share_var_set(THREAD_TASK_NAME, debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS));
		/*ob_start();
		ob_implicit_flush(false);
		echo THREAD_TASK_NAME, PHP_EOL;
		debug_print_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS);
		echo ob_get_clean();*/
	} else {
		task_set_run(false);
		// share_var_set('main', debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS));
	}
}
function signal_timeout(int $sig) {
	throw new TimeoutException('Execute timeout');
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);
pcntl_signal(SIGUSR1, 'signal', false);
pcntl_signal(SIGUSR2, 'signal', false);
pcntl_signal(SIGALRM, 'signal_timeout', false);

pthread_sigmask(SIG_SETMASK, []);

define('IS_TO_FILE', ($env = getenv('IS_TO_FILE')) === false ? true : !empty($env));

$statres = ts_var_declare('statres');

if(defined('THREAD_TASK_NAME')) {
	// echo THREAD_TASK_NAME . PHP_EOL;

	$aptres = ts_var_declare('aptres', null, true);
	$wsres = ts_var_declare('wsres', null, true);

	if(THREAD_TASK_NAME === 'ws') {
		$base = new EventBase();
		$events = [];
		$i = 0;
		$rfd = ts_var_fd($wsres);
		$event = new EventBufferEvent($base, $rfd, EventBufferEvent::OPT_CLOSE_ON_FREE, function($event, $arg) use(&$base, &$events, &$bufs, &$i, &$wsres, &$statres) {
			$n = strlen($event->read(256));
			for($j=0; $j<$n; $j++) {
				list($fd, $addr, $port) = ts_var_shift($wsres);

				$fd = socket_import_fd($fd);
				$event = new EventBufferEvent($base, $fd, EventBufferEvent::OPT_CLOSE_ON_FREE, function($event, $i) use(&$bufs, &$events, &$statres) {
					$buf = $event->read(16384);
					if($buf === false) {
						close:
						unset($events[$i], $bufs[$i]);
						return;
					}
					
					// unmask for message
					if(isset($bufs[$i])) {
						if(is_string($bufs[$i])) {
							$buf = $bufs[$i] . $buf;
							unset($bufs[$i]);
						} else {
							$masks = $bufs[$i][0];
							$buf = $bufs[$i][1] . $buf;
							$length = $bufs[$i][2];
							$ctl = $bufs[$i][3];
							
							$n = strlen($buf);
							if($n < $length) {
								$bufs[$i][1] = $buf;
								$buf = null;
								return;
							} elseif($n == $length) {
								unset($bufs[$i]);
							} else {
								$bufs[$i] = substr($buf, $length);
								$buf = substr($buf, 0, $length);
							}
							goto unmask;
						}
					}
				unpack:
					$n = strlen($buf);
					if($n < 6) {
						trybuf:
						$bufs[$i] = $buf;
						$buf = null;
						return;
					}
					$cc = unpack('C2', $buf);
					if(!($cc[1] & 0x80) || ($cc[1] & 0x70) || !($cc[2] & 0x80)) { // !FIN || (RSV1 || RSV2 || RSV3) || !Mask
						ts_var_inc($statres, 'error', 1);
						goto close;
					}
					$ctl = $cc[1] & 0xf;
					$length = $cc[2] & 0x7f;
					unset($cc);
					if($length == 126) {
						if($n < 8) goto trybuf;
						$length = unpack('n', $buf, 2)[1];
						$masks = substr($buf, 4, 4);
						$buf = substr($buf, 8);
					} elseif($length == 127) {
						if($n < 14) goto trybuf;
						$length = unpack('J', $buf, 2)[1];
						$masks = substr($buf, 10, 4);
						$buf = substr($buf, 14);
					} else {
						$masks = substr($buf, 2, 4);
						$buf = substr($buf, 6);
					}
					$n = strlen($buf);
					if($n < $length) {
						$bufs[$i] = [$masks, $buf, $length, $ctl];
						$buf = $masks = null;
						return;
					} elseif($n > $length) {
						$bufs[$i] = substr($buf, $length);
						$buf = substr($buf, 0, $length);
					}
				unmask:
					$text = "";
					for($_i = 0; $_i < $length; ++$_i) {
						$text .= $buf[$_i] ^ $masks[$_i % 4];
					}
					$buf = $text;

					switch($ctl) {
						case 0x1: // text
						case 0x2: // binary
							break;
						case 0x8: // close
							if($length > 2) {
								$errno = unpack('n', $buf)[1];
								$error = substr($buf, 2);
								echo "WebSocket($errno): $error\n";
							}
							goto close;
							break;
						case 0x9: // ping
							// echo "ping\n";
							if($event->write(mask($buf, 0x8a)) === false) goto close;
							goto next;
							break;
						case 0xA: // pong
							// echo "pong\n";
							goto next;
							break;
						default:
							ts_var_inc($statres, 'error', 1);
							goto close;
							break;
					}
					
					ts_var_inc($statres, 'success', 1);

					// mask for message
					$buf = mask($buf);

					// send message
					foreach($events as $j=>$evt) {
						if($evt->write($buf) === false) {
							unset($events[$j], $bufs[$j]);
						}
					}

				next:
					if(isset($bufs[$i])) {
						$buf = $bufs[$i];
						unset($bufs[$i]);
						goto unpack;
					}
				}, null, function($bev, $event, $arg) use(&$events) {
					if($event & (EventBufferEvent::EOF | EventBufferEvent::ERROR)) {
						unset($events[$arg]);
					}
				}, $i);
				
				$event->enable(Event::READ);
				$event->write(mask("$addr:$port connected"));
				$events[$i++] = $event;
				
				socket_export_fd($fd, true); // skip close socket
			}
		});
		$event->enable(Event::READ);
		
		$timer = new Event($base, -1, Event::TIMEOUT | Event::PERSIST, function() use(&$events) {
			// echo "PING\n";
			$buf = mask('ping', 0x89);
			foreach($events as $event) {
				$event->write($buf);
			}
		});
		$timer->addTimer(30);
		
		$timer = new Event($base, -1, Event::TIMEOUT | Event::PERSIST, function() use(&$base, &$running) {
			if(!$running) $base->exit();
		});
		$timer->addTimer(0.5);

		$base->loop();
		
		socket_export_fd($rfd, true); // skip close socket
	} elseif(strncmp(THREAD_TASK_NAME, 'accept', 6) == 0) {
		//$sock = socket_import_fd((int) $_SERVER['argv'][1]);
		$sock = (int) $_SERVER['argv'][1];
		$flag = (bool) $_SERVER['argv'][2];
		$wfd = ts_var_fd($aptres, true);

		while($running && ($fd = @socket_accept_ex($sock, $addr, $port)) !== false) {
			@socket_set_option($fd, SOL_SOCKET, SO_LINGER, ['l_onoff'=>1, 'l_linger'=>1]) or strerror('socket_set_option', false);
			$i = ts_var_inc($statres, 'conns', 1);
			$fd = socket_export_fd($fd, true);
			ts_var_set($aptres, $i, [$fd,$addr,$port]);
			if($flag || ts_var_inc($statres, 'accept', -1) < 0) {
				create_task('read' . $i, __FILE__, [1]);
				if(!$flag) ts_var_inc($statres, 'accept', 1);
			} else @socket_write($wfd, 'a', 1);
		}

		//socket_export_fd($sock, true); // skip close socket
		socket_export_fd($wfd, true); // skip close socket

		//echo THREAD_TASK_NAME . " Closed\n";
	} elseif(empty($_SERVER['argv'][1])) {
		$rfd = ts_var_fd($aptres);
		$wfd = ts_var_fd($wsres, true);
		ts_var_inc($statres, 'accept', 1);
		while($running) {
			if(!($a = @socket_read($rfd, 1))) continue;
			if($a !== 'a') break;

			list($fd, $addr, $port) = ts_var_shift($aptres);

			$fd = socket_import_fd($fd);

			$t = microtime(true);
			do {
				$request = new HttpRequest($fd, $addr, $port, 'onBody');
				while(($ret = $request->read()) === false);
				if(!$request->isHTTP) break;

				// var_dump($request, $ret);

				if($request->isKeepAlive && microtime(true) - $t >= min(10, $request->headers['Keep-Alive']??10)) {
					$request->isKeepAlive = false;
				}

				if($ret) {
					$response = $request->getResponse();
					if($request->isKeepAlive) {
						$response->headers['Connection'] = 'keep-alive';
					} else {
						$response->headers['Connection'] = 'close';
					}
					if($response->end(onRequest($request, $response))) {
						ts_var_inc($statres, 'success', 1);

						if($response->isWebSocket) {
							$request->isKeepAlive = false;

							$fd = socket_export_fd($fd, true);
							ts_var_push($wsres, [$fd,$addr,$port]);
							@socket_write($wfd, 'a');
						}
					} else {
						ts_var_inc($statres, 'error', 1);
						break;
					}
				} else {
					if($ret === 0) {
						$response = $request->getResponse(400, 'Bad Request');
						$response->setContentType('text/plain');
						$response->headers['Connection'] = 'close';
						$response->end('Bad Request');
					}
					ts_var_inc($statres, 'error', 1);
				}
			} while($ret && $request->isKeepAlive);

			$request = $response = $addr = null;

			//is_int($fd) or @socket_shutdown($fd) or strerror('socket_shutdown', false);
			is_int($fd) or @socket_close($fd);
			
			ts_var_inc($statres, 'accept', 1);
		}

		socket_export_fd($rfd, true); // skip close socket
		socket_export_fd($wfd, true); // skip close socket

		//echo THREAD_TASK_NAME . " Closed\n";
	} else {
		list($fd, $addr, $port) = ts_var_shift($aptres);

		$fd = socket_import_fd($fd);

		$t = microtime(true);
		do {
			$request = new HttpRequest($fd, $addr, $port, 'onBody');
			while(($ret = $request->read()) === false);
			if(!$request->isHTTP) break;

			// var_dump($request, $ret);

			if($request->isKeepAlive && microtime(true) - $t >= 10) {
				$request->isKeepAlive = false;
			}

			if($ret) {
				$response = $request->getResponse();
				if($request->isKeepAlive) {
					$response->headers['Connection'] = ($request->headers['Connection'] ?? 'Keep-Alive');
				} else {
					$response->headers['Connection'] = 'close';
				}
				if($response->end(onRequest($request, $response))) {
					ts_var_inc($statres, 'success', 1);
					
					if($response->isWebSocket) {
						$request->isKeepAlive = false;

						$fd = socket_export_fd($fd, true);
						ts_var_push($wsres, [$fd,$addr,$port]);
						$wfd = ts_var_fd($wsres, true);
						@socket_write($wfd, 'a');
						socket_export_fd($wfd, true);
					}
				} else {
					ts_var_inc($statres, 'error', 1);
					break;
				}
			} else {
				if($ret === 0) {
					$response = $request->getResponse(400, 'Bad Request');
					$response->setContentType('text/plain');
					$response->headers['Connection'] = 'close';
					$response->end('Bad Request');
				}
				ts_var_inc($statres, 'error', 1);
			}
		} while($ret && $request->isKeepAlive);

		//is_int($fd) or @socket_shutdown($fd) or strerror('socket_shutdown', false);
		is_int($fd) or @socket_close($fd);
	}
} else {
	$host = ($_SERVER['argv'][1]??'127.0.0.1');
	$port = ($_SERVER['argv'][2]??5000);
	$flag = ($_SERVER['argv'][3]??0);

	($sock = @socket_create(AF_INET, SOCK_STREAM, SOL_TCP)) or strerror('socket_create');
	@socket_set_option($sock, SOL_SOCKET, SO_REUSEADDR, 1) or strerror('socket_set_option', false);
	@socket_bind($sock, $host, (int)$port) or strerror('socket_bind');
	@socket_listen($sock, 128) or strerror('socket_listen');

	$fd = socket_export_fd($sock);

	echo "Server listening on $host:$port\n";

	share_var_init(3);

	$aptres = ts_var_declare('aptres', null, true);
	$wsres = ts_var_declare('wsres', null, true);

	create_task('ws', __FILE__, []);
	if(!$flag) for($i=0; $i<100; $i++) create_task('read' . $i, __FILE__, []);
	for($i=0; $i<4; $i++) create_task('accept' . $i, __FILE__, [$fd,$flag]);

	$base = new EventBase();
	$n = $ns = $ne = 0;
	
	$timer = new Event($base, -1, Event::TIMEOUT | Event::PERSIST, function() use(&$statres, &$aptres, &$n, &$ns, &$ne) {
		$n2 = ts_var_get($statres, 'conns');
		$n3 = ts_var_count($aptres);
		$n4 = ts_var_get($statres, 'success');
		$n5 = ts_var_get($statres, 'error');
		$n = $n2 - $n;
		$ns = $n4 - $ns;
		$ne = $n5 - $ne;
		echo "$n connects, $n3 accepts, $ns successes, $ne errors\n";
		$n = $n2;
		$ns = $n4;
		$ne = $n5;
	});
	$timer->addTimer(1);
	
	$timer2 = new Event($base, -1, Event::TIMEOUT | Event::PERSIST, function() use(&$base, &$running) {
		trigger_timeout();

		if(!$running) $base->exit();
	});
	$timer2->addTimer(0.01);

	$base->loop();

	$wfd = ts_var_fd($aptres, true);
	@socket_write($wfd, str_repeat('e', task_get_threads(true)));
	socket_export_fd($wfd, true); // skip close socket
	unset($wfd);

	task_wait($exitSig?:SIGINT);

	$accepts = ts_var_get($aptres) + ts_var_get($wsres);

	foreach($accepts as $i=>$_fd) {
		list($fd, $addr, $port) = $_fd;
		echo $str = "unread($addr:$port): $i=>$fd\n";
		$fd = socket_import_fd($fd);
		@socket_shutdown($fd) or strerror('socket_shutdown', false);
		@socket_close($fd);
	}

	@socket_shutdown($sock) or strerror('socket_shutdown');
	@socket_close($sock);

	// $vars = share_var_get();
	// ksort($vars);
	// echo json_encode($vars, JSON_PRETTY_PRINT);

	share_var_destory();

	echo "Stoped\n";
}

function strerror(string $msg, bool $isExit = true) {
	$err = socket_last_error();
	socket_clear_error();
	if($err === SOCKET_EINTR) return;

	ob_start();
	ob_implicit_flush(false);
	debug_print_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS);
	$trace = ob_get_clean();
	printf("[%s] %s(%d): %s\n%s", defined('THREAD_TASK_NAME') ? THREAD_TASK_NAME : 'main', $msg, $err, socket_strerror($err), $trace);

	if($isExit) exit; else return true;
}

function mask(string $txt, int $ctl = 0x81) {
	$n = strlen($txt);
	if($n <= 125)
		return pack('CC', $ctl, $n) . $txt;
	elseif($n > 125 && $n < 65536)
		return pack('CCn', $ctl, 126, $n) . $txt;
	else
		return pack('CCJ', $ctl, 127, $n) . $txt;
}

function onBody(HttpRequest $request): bool {
	$request->isDav = strncmp($request->path . '/', '/dav/', 5) === 0;

	if($request->isDav) {
		if($request->method === 'POST' || $request->method === 'PUT') {
			$fp = @fopen(__DIR__ . $request->path, 'wb+');
			if($fp) {
				$request->setFp($fp);
			} else {
				$response = $request->getResponse(404, 'Not Found');
				$response->end('<h1>Not Found: fopen failure</h1>');
				return false;
			}
		}
	} elseif($request->bodylen > 8*1024*1024) {
		$response = $request->getResponse(413, 'Request Entity Too Large');
		$response->end('<h1>Request Entity Too Large</h1>');
		return false;
	}
	
	return true;
}

class TimeoutException extends Exception {
}

function onRequest(HttpRequest $request, HttpResponse $response): ?string {
	switch($request->path) {
		case '/request-info':
			if(isset($request->get['json'])) {
				$response->setContentType('application/json; charset=utf-8');
				return json_encode($request, JSON_PRETTY_PRINT|JSON_UNESCAPED_UNICODE|JSON_UNESCAPED_SLASHES);
			} else {
				$response->setContentType('text/plain');
				return var_export($request, true);
			}
		case '/hello-json':
			$response->setContentType('application/json');
			return json_encode(['method'=>$request->method, 'message'=>'Hello threadtask!']);
		case '/null':
			unset($response->headers['Content-Type']);
			return null;
		case '/chunked':
			$n = rand(5, 50);
			for($i=0;$i<$n;$i++) $response->write("LINE: $i/$n\r\n");
			return $n % 2 === 0 ? null : "END: $i/$n\r\n";
		case '/ws':
			if(!isset($request->headers['Upgrade'], $request->headers['Sec-WebSocket-Key'], $request->headers['Sec-WebSocket-Version']) || empty($request->headers['Sec-WebSocket-Key']) || $request->headers['Upgrade'] !== 'websocket') {
				$response->status = 404;
				$response->statusText = 'Not Found';
				return '<h1>Not Found</h1>';
			}
			$host = $request->headers['Host'] ?? '127.0.0.1:5000';
			$secAccept = base64_encode(pack('H*', sha1($request->headers['Sec-WebSocket-Key'] . '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')));
			$response->status = 101;
			$response->statusText = 'Web Socket Protocol Handshake';
			$response->headers['Upgrade'] = 'websocket';
			$response->headers['Connection'] = 'Upgrade';
			$response->headers['WebSocket-Origin'] = $host;
			$response->headers['WebSocket-Location'] = 'ws://' . $host . $request->path;
			$response->headers['Sec-WebSocket-Version'] = $request->headers['Sec-WebSocket-Version'];
			$response->headers['Sec-WebSocket-Accept'] = $secAccept;
			$response->isWebSocket = true;
			return null;
		case '/setcookie':
			$response->setCookie(
				$request->get['name'] ?? 'test',
				$request->get['value'] ?? null,
				(int) ($request->get['expires'] ?? 0),
				$request->get['path'] ?? '',
				$request->get['domain'] ?? '',
				!empty($request->get['secure']),
				!empty($request->get['httponly']),
				$request->get['samesite'] ?? ''
			);
			return null;
		case '/timeout':
			set_timeout(1);
			$t = microtime(true);
			try {
				sleep(5);
				return null;
			} catch(TimeoutException $e) {
				$response->status = 408;
				$response->statusText = 'Request Timeout';
				$response->headers['Run-Time'] = microtime(true) - $t;
				return '<h1>Request Timeout</h1>';
			} finally {
				clear_timeout();
			}
		case '/mysql':
			global $DB, $CFG, $DB_time, $DB_wait;
			try {
				$t = microtime(true);
				if(!$DB || $DB_time < $t || array_diff($CFG, $request->get['cfg']??[])) {
					$CFG = $request->get['cfg']??[];
					$DB = new PDO('mysql:dbname=' . ($CFG['db']??'test') . ';host=' . ($CFG['host']??'localhost'), $CFG['user']??'root', $CFG['pass']??'123456');
					$query = $DB->prepare('SELECT @@wait_timeout');
					$query->execute();
					$DB_wait = (int) $query->fetchColumn(0);
					$response->headers['Wait-Timeout'] = $DB_wait;
				}
				$query = $DB->prepare($request->get['query']??'show tables');
				$query->execute();
				$DB_time = $t + $DB_wait;
				$response->setContentType('application/json; charset=utf-8');
				return json_encode($query->fetchAll(PDO::FETCH_OBJ));
			} catch(PDOException $e) {
				$response->setContentType('text/plain; charset=utf-8');
				return (string) $e;
			}
			break;
		default:
			if($request->isDav) {
				$path = __DIR__ . $request->path;
				switch($request->method) {
					case 'OPTIONS':
						$response->headers['Allow'] = 'HEAD, GET, PUT, MKCOL, DELETE';
						return null;
					case 'POST':
					case 'PUT':
						if(($n = @filesize($path)) !== $request->bodylen) {
							$response->status = 500;
							$response->statusText = 'Internal Server Error';
							return $n === false ? 'Upload failure' : 'File size not equal';
						} else {
							return 'OK';
						}
						break;
					case 'MKCOL':
						if(is_dir($path)) {
							return 'The directory already exists';
						} elseif(@mkdir($path, 0755, true)) {
							return 'OK';
						} else {
							$response->status = 500;
							$response->statusText = 'Internal Server Error';
							return 'mkdir failure';
						}
					case 'DELETE':
						if(@unlink($path)) {
							return 'OK';
						} else {
							$response->status = 500;
							$response->statusText = 'Internal Server Error';
							return 'Delete failure';
						}
						break;
				}
			} else {
				$path = __DIR__ . $request->path;
			}
			break;
	}

	switch($request->method) {
		case 'OPTIONS':
			$response->headers['Allow'] = 'HEAD, GET';
			return null;
		case 'HEAD':
		case 'GET':
			break;
		default:
			$response->status = 405;
			$response->statusText = 'Method Not Allowed';

			return '<h1>Method Not Allowed</h1>';
	}

	if(is_dir($path)) {
		$files = [];
		$_path = rtrim($request->path, '/') . '/';
		if(($dh = @opendir($path)) !== false) {
			while(($f=readdir($dh)) !== false) {
				if($f === '.' || $f === '..') continue;
				
				$st = stat($path . '/' . $f);
				$files[] = [
					'name' => $f,
					'url' => $_path . $f,
					'size' => $st['size'],
					'perms' => getperms($st['mode'], $type),
					'type' => $type,
					'atime' => $st['atime'],
					'mtime' => $st['mtime'],
					'ctime' => $st['ctime'],
				];
			}
			closedir($dh);
		}
		
		$key = ($request->get['key'] ?? 'name');
		$sort = ($request->get['sort'] ?? 'asc');

		if($files) {
			if($key === 'url' || !isset($files[0][$key])) $key = 'name';
			switch($key) {
				case 'size':
				case 'atime':
				case 'mtime':
				case 'ctime':
					$call = function(array $a, array $b) use($key) {return $a[$key] <=> $b[$key];};
					break;
				default:
					$call = function(array $a, array $b) use($key) {return strcmp($a[$key], $b[$key]);};
					break;
			}
			
			if($sort === 'asc') usort($files, $call);
			else usort($files, function(array $a, array $b) use($call) {return -$call($a, $b);});
		}

		if(isset($request->get['json'])) {
			$response->setContentType('application/json; charset=utf-8');
			return json_encode($files, JSON_PRETTY_PRINT|JSON_UNESCAPED_UNICODE|JSON_UNESCAPED_SLASHES);
		}

		ob_start();
		ob_implicit_flush(false);
?><!DOCTYPE html>
<html lang="zh-cn">
<head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <meta name="referrer" content="origin" />
    <meta http-equiv="Cache-Control" content="no-transform" />
    <meta http-equiv="Cache-Control" content="no-siteapp" />
    <meta http-equiv="X-UA-Compatible" content="IE=edge" />
    <title><?=$request->path?></title>
    <style type="text/css">
    body{margin:0;padding:5px;}
    table{border:1px #ccc solid;border-width:1px 0 0 1px;border-spacing:0;margin:0 auto;}
    th,td{border:1px #ccc solid;padding:5px;}
    td{border-width:0 1px 1px 0;}
    th{border-width:0 1px 2px 0;}
    </style>
</head>
<body>
<table>
	<thead>
		<tr><?php
		$titles = [
			'name' => 'Name',
			'size' => 'Size',
			'type' => 'Type',
			'perms' => 'Perm',
			'atime' => 'Time of last access',
			'mtime' => 'Time of last modification',
			'ctime' => 'Time of last modification',
		];
		foreach($titles as $k=>$t):
			if($k === $key):
				?><th><a href="?key=<?=$k?><?=($sort === 'asc' ? '&sort=desc' : null)?>"><?=$t?><?=($sort === 'asc' ? '↑' : '↓')?></a></th><?php
			else:
				?><th><a href="?key=<?=$k?>"><?=$t?></a></th><?php
			endif;
		endforeach;
		?></tr>
	</thead>
	<tbody><?php
	if($request->path !== '/'):
		?><tr><td colspan="7"><a href="<?=dirname($request->path)?>">..</a></td></tr><?php
	endif;
	foreach($files as $file):
		?><tr>
			<td><a href="<?=$file['url']?>"><?=$file['name']?></a></td>
			<td><?=$file['size']?></td>
			<td><?=$file['type']?></td>
			<td><?=$file['perms']?></td>
			<td><?=date('Y-m-d H:i:s', $file['atime'])?></td>
			<td><?=date('Y-m-d H:i:s', $file['mtime'])?></td>
			<td><?=date('Y-m-d H:i:s', $file['ctime'])?></td>
		</tr><?php
	endforeach;
	?></tbody>
</table>
</body>
</html>
<?php
		return ob_get_clean();
	} elseif(is_file($path)) {
		if(isset($request->get['format']) && pathinfo($request->path, PATHINFO_EXTENSION) === 'php') {
			return ($buf = highlight_file($path, true)) === false ? null : $buf;
		}

		if(($fp = @fopen($path, 'rb+')) !== false) {
			$response->setContentType(@mime_content_type($path) ?: 'application/octet-stream');
			$stat = @fstat($fp);
			if($stat === false) {
				fclose($fp);
				
				$response->status = 404;
				$response->statusText = 'Not Found';
				
				return '<h1>Not Found</h1>';
			}
			$response->headers['Last-Modified'] = gmdate('D, d-M-Y H:i:s T', $stat['mtime']);
			$response->headers['ETag'] = sprintf('%xT-%xO', $stat['mtime'], $stat['size']);
			$response->headers['Accept-Ranges'] = 'bytes';
			$response->headers['Expires'] = gmdate('D, d-M-Y H:i:s T', time() + 60);
			$response->headers['Cache-Control'] = 'max-age=60';

			if(isset($request->headers['If-None-Match'])) {
				if($request->headers['If-None-Match'] === $response->headers['ETag']) {
					$response->headers['If-None-Match'] = 'false';
					$response->status = 304;
					$response->statusText = 'Not Modified';
					@fclose($fp);
					return null;
				} else {
					$response->headers['If-None-Match'] = 'true';
				}
			}

			if(isset($request->headers['If-Modified-Since'])) {
				if($request->headers['If-Modified-Since'] === $response->headers['Last-Modified']) {
					$response->headers['If-Modified-Since'] = 'true';
					$response->status = 304;
					$response->statusText = 'Not Modified';
					@fclose($fp);
					return null;
				} else {
					$response->headers['If-Modified-Since'] = 'false';
				}
			}

			if(isset($request->headers['If-Unmodified-Since'])) {
				if(strcmp($request->headers['If-Unmodified-Since'], $response->headers['Last-Modified']) < 0) {
					$response->headers['If-Unmodified-Since'] = 'false';
					$response->status = 412;
					$response->statusText = 'Precondition failed';
					@fclose($fp);
					return null;
				} else {
					$response->headers['If-Unmodified-Since'] = 'true';
				}
			}

			if(isset($request->headers['If-Range'])) {
				if(isset($request->headers['Range']) && $request->headers['If-Range'] === $response->headers['Last-Modified']) {
					goto range;
				}
			} elseif(isset($request->headers['Range'])) {
				range:
				$ranges = [];
				$range = $request->headers['Range'];
				$n = preg_match_all('/(bytes=)?([0-9]+)\-([0-9]*),?\s*/i', $request->headers['Range'], $matches);

				if($n && implode('', $matches[0]) === $range) {
					for($i=0; $i<$n; $i++) {
						$range = $matches[3][$i];
						$start = $matches[2][$i];
						$end = ($range === '' ? $stat['size']-1 : $range);
						$ranges[] = [$start, $end];
						if($start < 0 || $start > $stat['size'] || $end < 0 || $end > $stat['size']) {
							$response->status = 416;
							$response->statusText = 'Requested Range Not Satisfiable';
							@fclose($fp);
							return null;
						}
					}
					if($n > 1) {
						$boundary = bin2hex(random_bytes(8));
						$boundaryEnd = "--$boundary--\r\n";
						$contentType = $response->headers['Content-Type'];
						$response->status = 206;
						$response->statusText = 'Partial Content';
						$response->headers['Content-Type'] = 'multipart/byteranges; boundary=' . $boundary;
						$size = strlen($boundaryEnd);
						foreach($ranges as &$range) {
							$range[2] = "--$boundary\r\nContent-Type: {$contentType}\r\nContent-Range: bytes {$range[0]}-{$range[1]}/{$stat['size']}\r\n\r\n";
							$size += strlen($range[2]) + $range[1] - $range[0] + 3;
						}
						unset($range);
						$response->headSend($size);
						
						foreach($ranges as $range) {
							if(!$response->send($range[2])) {
								@fclose($fp);
								return null;
							}
							$size = $range[1] - $range[0] + 1;
							@fseek($fp, $range[0], SEEK_SET);
							while($size > 0 && !@feof($fp)) {
								if(($buf = @fread($fp, min(8192, $size))) === false) {
									$request->isKeepAlive = false;
									@fclose($fp);
									return null;
								} else {
									if(!$response->send($buf)) {
										@fclose($fp);
										return null;
									}
									$size -= strlen($buf);
								}
							}
							if(!$response->send("\r\n")) {
								@fclose($fp);
								return null;
							}
						}
						
						$response->send($boundaryEnd);
					} else {
						$size = $ranges[0][1] - $ranges[0][0] + 1;
						if($size === $stat['size']) goto all;
						$response->status = 206;
						$response->statusText = 'Partial Content';
						$response->headers['Content-Range'] = "bytes {$ranges[0][0]}-$ranges[0][1]/{$stat['size']}";
						$response->headSend($size);
						fseek($fp, $ranges[0][0], SEEK_SET);
						while($size > 0 && !@feof($fp)) {
							if(($buf = @fread($fp, min($size, 8192))) === false) {
								$request->isKeepAlive = false;
								break;
							} else {
								if(!$response->send($buf)) break;
								$size -= strlen($buf);
							}
						}
					}
					@fclose($fp);
					return null;
				}
			}

		all:
			$response->headSend($stat['size']);
			if($request->method === 'GET') {
				while(!@feof($fp)) {
					if(($buf = @fread($fp, 8192)) === false) {
						$request->isKeepAlive = false;
						break;
					} else {
						if(!$response->send($buf)) break;
					}
				}
			}
			@fclose($fp);
		}
		return null;
	} else {
		$response->status = 404;
		$response->statusText = 'Not Found';
		
		return '<h1>Not Found</h1>';
	}
}

function getperms(int $mode, ?string &$type = null) {
	if (($mode & 0xC000) == 0xC000) {
		// Socket
		$info = 's';
		$type = 'Socket';
	} elseif (($mode & 0xA000) == 0xA000) {
		// Symbolic Link
		$info = 'l';
		$type = 'Symbolic Link';
	} elseif (($mode & 0x8000) == 0x8000) {
		// Regular
		$info = '-';
		$type = 'Regular';
	} elseif (($mode & 0x6000) == 0x6000) {
		// Block special
		$info = 'b';
		$type = 'Block special';
	} elseif (($mode & 0x4000) == 0x4000) {
		// Directory
		$info = 'd';
		$type = 'Directory';
	} elseif (($mode & 0x2000) == 0x2000) {
		// Character special
		$info = 'c';
		$type = 'Character special';
	} elseif (($mode & 0x1000) == 0x1000) {
		// FIFO pipe
		$info = 'p';
		$type = 'FIFO pipe';
	} else {
		// Unknown
		$info = 'u';
		$type = 'Unknown';
	}

	// Owner
	$info .= (($mode & 0x0100) ? 'r' : '-');
	$info .= (($mode & 0x0080) ? 'w' : '-');
	$info .= (($mode & 0x0040) ?
		        (($mode & 0x0800) ? 's' : 'x' ) :
		        (($mode & 0x0800) ? 'S' : '-'));

	// Group
	$info .= (($mode & 0x0020) ? 'r' : '-');
	$info .= (($mode & 0x0010) ? 'w' : '-');
	$info .= (($mode & 0x0008) ?
		        (($mode & 0x0400) ? 's' : 'x' ) :
		        (($mode & 0x0400) ? 'S' : '-'));

	// World
	$info .= (($mode & 0x0004) ? 'r' : '-');
	$info .= (($mode & 0x0002) ? 'w' : '-');
	$info .= (($mode & 0x0001) ?
		        (($mode & 0x0200) ? 't' : 'x' ) :
		        (($mode & 0x0200) ? 'T' : '-'));
	return $info;
}

class HttpRequest {
	public $clientAddr = null;
	public $clientPort = 0;
	public $readlen = 0;

	public $head = null;
	
	public $method = null;
	public $uri = null;
	public $protocol = null;
	public $isHTTP = false;

	public $path = null;
	public $get = [];

	public $headers = [];
	public $cookies = [];
	public $post = [];
	public $files = [];
	
	public $isKeepAlive = false;

	private $fd;
	
	public $mode = self::MODE_FIRST;
	public $buf = null;
	public $bodymode = 0;
	public $bodytype = null;
	public $bodyargs = [];
	public $bodylen = 0;
	public $bodyoff = 0;
	
	public $isDav = false;
	
	private $onBody = null;
	
	private $isToFile = IS_TO_FILE;
	private $fp = null;
	private $formmode = self::FORM_MODE_BOUNDARY;
	private $boundary = null;
	private $boundaryBgn = null;
	private $boundaryPos = null;
	private $boundaryEnd = null;
	private $formheaders = [];
	private $formargs = [];

	const MODE_FIRST = 1;
	const MODE_HEAD = 2;
	const MODE_BODY = 3;
	const MODE_END = 4;
	
	const BODY_MODE_URL_ENCODED = 1;
	const BODY_MODE_FORM_DATA = 2;
	const BODY_MODE_JSON = 3;
	
	const FORM_MODE_BOUNDARY = 1;
	const FORM_MODE_HEAD = 2;
	const FORM_MODE_VALUE = 3;

	public function __construct($fd, string $addr, int $port, ?callable $onBody = null) {
		$this->fd = $fd;
		$this->clientAddr = $addr;
		$this->clientPort = $port;
		$this->onBody = $onBody;
	}
	
	public function __destruct() {
		if($this->fp) {
			fclose($this->fp);
			$this->fp = null;
		}
	}
	
	public function setFp($fp) {
		if($this->bodylen > 0 && $this->bodyoff === 0) $this->fp = $fp;
	}
	
	public function getResponse(int $status = 200, $statusText = 'OK'): HttpResponse {
		return new HttpResponse($this->fd, $this->protocol??'HTTP/1.0', $status, $statusText);
	}

	public function read() {
		if($this->mode === self::MODE_END) return true;
		
		$n = @socket_recv($this->fd, $buf, 16384, 0);
		if($n === false) {
			if($this->readlen) strerror('socket_recv', false);
			else socket_clear_error();
			
			return null;
		}
		if($n <= 0) {
			return null;
		}
		
		$this->readlen += $n;
		
		// echo $buf;

		if($this->buf !== null) {
			$n += strlen($this->buf);
			$buf = $this->buf . $buf;
			$this->buf = null;
		}

		$i = 0;
		while($i < $n) {
			switch($this->mode) {
				case self::MODE_FIRST:
					$pos = strpos($buf, "\r\n", $i);
					if($pos === false) {
						$this->buf = substr($buf, $i);
						$i = $n;
					} else {
						$this->head = substr($buf, $i, $pos-$i);
						@list($this->method, $this->uri, $this->protocol) = explode(' ', $this->head, 3);
						$uri = parse_url($this->uri);
						if(isset($uri['path'])) {
							$this->path = $uri['path'];
							if(strpos($this->path, '%') !== false) $this->path = urldecode($this->path);
							$this->isHTTP = preg_match('/HTTP\/1\.[01]/', $this->protocol) > 0;
						}
						if(isset($uri['query'])) {
							parse_str($uri['query'], $this->get);
						}
						$i = $pos + 2;
						$this->mode = self::MODE_HEAD;
					}
					break;
				case self::MODE_HEAD:
					$pos = strpos($buf, "\r\n", $i);
					if($pos === false) {
						$this->buf = substr($buf, $i);
						$i = $n;
					} elseif($i === $pos) {
						$i += 2;

						$this->bodylen = (int) ($this->headers['Content-Length'] ?? 0);
						if($this->bodylen < 0) $this->bodylen = 0;
						$this->mode = ($this->bodylen ? self::MODE_BODY : self::MODE_END);
						
						$this->isKeepAlive = (isset($this->headers['Connection']) && !strcasecmp($this->headers['Connection'], 'keep-alive'));

						$args = $this->getHeadArgs($this->headers['Content-Type'] ?? '');
						$this->bodytype = $args[0];
						unset($args[0]);
						$this->bodyargs = $args;
						
						if(!empty($this->headers['Cookie'])) {
							$cookies = (array) $this->headers['Cookie'];
							foreach($cookies as $cookie) {
								if(($cookie = preg_split('/;\s+/', $cookie, -1, PREG_SPLIT_NO_EMPTY)) !== false) {
									foreach($cookie as $_cookie) {
										@list($name, $value) = explode('=', $_cookie, 2);
										$this->cookies[$name] = urldecode($value);
									}
								}
							}
						}
						
						if(($onBody = $this->onBody) !== null && !$onBody($this)) {
							return null;
						}

						if(isset($this->headers['Expect']) && $this->headers['Expect'] === '100-continue') {
							if(!$this->send("{$this->protocol} 100 Continue\r\n\r\n")) return null;
						}

						switch($this->bodytype) {
							case 'application/x-www-form-urlencoded':
								$this->bodymode = self::BODY_MODE_URL_ENCODED;
								break;
							case 'multipart/form-data':
								$this->bodymode = self::BODY_MODE_FORM_DATA;
								$this->boundary = $this->bodyargs['boundary'];
								$this->boundaryBgn = '--' . $this->boundary;
								$this->boundaryPos = "\r\n--{$this->boundary}";
								$this->boundaryEnd = '--' . $this->boundary . '--';
								break;
							case 'application/json':
								$this->bodymode = self::BODY_MODE_JSON;
								break;
							default:
								break;
						}
					} else {
						@list($name, $value) = preg_split('/:\s*/', substr($buf, $i, $pos-$i), 2);
						if(isset($this->headers[$name])) {
							$_value = & $this->headers[$name];
							if(!is_array($_value)) {
								$_value = (array) $_value;
							}
							$_value[] = $value;
						} else {
							$this->headers[$name] = $value;
						}
						$i = $pos + 2;
					}
					break;
				case self::MODE_BODY:
					// echo 'BODYOFF: ', $n-$i, "\n";
					$this->bodyoff += $n - $i - strlen($this->buf);
					if($this->bodymode === self::BODY_MODE_FORM_DATA) {
						while($i < $n) {
							switch($this->formmode) {
								case self::FORM_MODE_BOUNDARY:
									$pos = strpos($buf, "\r\n", $i);
									if($pos === false) {
										$this->buf = substr($buf, $i);
										$i = $n;
									} elseif(($boundary = substr($buf, $i, $pos-$i)) === $this->boundaryBgn) {
										$this->formmode = self::FORM_MODE_HEAD;
										$i = $pos + 2;
									} elseif($boundary === $this->boundaryEnd) {
										$this->mode = self::MODE_END;
										$this->buf = null;
										$i = $n;
									} else {
										$this->buf = substr($buf, $i);
										// var_dump('BOUNDARY', $boundary, $this->boundaryBgn, $this->boundaryEnd);
										return 0;
									}
									break;
								case self::FORM_MODE_HEAD:
									$pos = strpos($buf, "\r\n", $i);
									if($pos === false) {
										$this->buf = substr($buf, $i);
										$i = $n;
									} elseif($i === $pos) {
										$this->formmode = self::FORM_MODE_VALUE;
										$i += 2;
										
										$this->formargs = $this->getHeadArgs($this->formheaders['Content-Disposition']);
										if($this->formargs[0] !== 'form-data') {
											$this->buf = null;
											return 0;
										}
										if(isset($this->formargs['filename'], $this->formheaders['Content-Type'])) {
											if($this->isToFile) {
												$path = tempnam(ini_get('upload_tmp_dir') ?: sys_get_temp_dir(), 'TTHS_');
												$this->fp = fopen($path, 'wb+');
											} else {
												$this->fp = null;
												$path = null;
											}
											$this->files[$this->formargs['name']] = ['name'=>$this->formargs['filename'], 'type'=>$this->formheaders['Content-Type'], 'path'=>$path];
										}
									} else {
										@list($name, $value) = preg_split('/:\s*/', substr($buf, $i, $pos-$i), 2);
										$this->formheaders[$name] = $value;
										$i = $pos + 2;
									}
									break;
								case self::FORM_MODE_VALUE:
									$pos = strpos($buf, $this->boundaryPos, $i);
									if($pos === false) {
										if($this->fp) {
											$j = $n - $i - strlen($this->boundaryPos) + 1;
											if($j > 0) {
												fwrite($this->fp, substr($buf, $i, $j), $j);
												$this->buf = substr($buf, $i + $j);
											} else {
												$this->buf = substr($buf, $i);
											}
										} else {
											$this->buf = substr($buf, $i);
										}
										$i = $n;
									} else {
										$value = substr($buf, $i, $pos - $i);
										if($this->fp) {
											fwrite($this->fp, $value, strlen($value));
											fclose($this->fp);
											$this->fp = null;
										} else {
											$this->post[$this->formargs['name']] = $value;
										}
										$i = $pos + 2;
										$this->formmode = self::FORM_MODE_BOUNDARY;
										$this->formheaders = $this->formargs = [];
									}
									break;
							}
						}

						if($this->bodyoff >= $this->bodylen && $this->mode !== self::MODE_END) {
							echo "BODYOFF: $buf\n";
							return 0;
						}
					} elseif($this->fp) {
						if($i === 0) {
							fwrite($this->fp, $buf, $n);
						} else {
							fwrite($this->fp, substr($buf, $i), $n - $i);
						}
						if($this->bodyoff >= $this->bodylen) {
							fclose($this->fp);
							$this->fp = null;
							$this->mode = self::MODE_END;
						}
					} else {
						$this->buf = substr($buf, $i);
						if($this->bodyoff >= $this->bodylen) {
							$this->mode = self::MODE_END;
							switch($this->bodymode) {
								case self::BODY_MODE_URL_ENCODED:
									parse_str($this->buf, $this->post);
									break;
								case self::BODY_MODE_JSON:
									$json = json_decode($this->buf, true);
									$errno = json_last_error();
									if($errno === JSON_ERROR_NONE) {
										if(is_array($json)) {
											$this->post = & $json;
										}
									} else {
										fprintf(STDERR, "JSON(%d): %s\n", $errno, json_last_error_msg());
									}
									break;
								default:
									
									break;
							}
						}
					}
					$i = $n;
					break;
				default: // MODE_END
					$this->buf = null;
					$i = $n;
					break;
			}
		}

		return $this->mode === self::MODE_END;
	}
	
	public function getHeadArgs($head): array {
		@list($arg0, $args) = preg_split('/;\s*/', $head, 2);
		$ret = [$arg0];
		$n = preg_match_all('/([^\=]+)\=("([^"]*)"|\'([^\']*)\'|([^;]*));?\s*/', $args, $matches);
		for($i=0; $i<$n; $i++) {
			$ret[$matches[1][$i]] = implode('', [$matches[3][$i], $matches[4][$i], $matches[5][$i]]);
		}
		// var_dump($ret, $head);
		return $ret;
	}
	
	private function send(string $data): bool {
		loop:
		$n = strlen($data);
		if($n === 0) return true;
		$ret = @socket_send($this->fd, $data, $n, 0);
		if($ret > 0) {
			if($ret < $n) {
				$data = substr($data, $ret);
				goto loop;
			} else return true;
		} else {
			strerror('socket_send', false);
			return false;
		}
	}
}

class HttpResponse {
	public $protocol;
	public $status;
	public $statusText;
	
	private $fd;
	
	private $isHeadSent = false;
	public $headers = ['Content-Type'=>'text/html; charset=utf-8', 'Task-Name'=>THREAD_TASK_NAME];
	public $isChunked = false;
	public $isEnd = false;
	public $isWebSocket = false;
	
	public function __construct($fd, string $protocol, int $status = 200, $statusText = 'OK') {
		$this->fd = $fd;
		$this->protocol = $protocol;
		$this->status = $status;
		$this->statusText = $statusText;
	}
	
	public function headSend(int $bodyLen = 0): bool {
		if($this->isHeadSent) return true;
		$this->isHeadSent = true;
		
		if($bodyLen >= 0) {
			$this->headers['Content-Length'] = $bodyLen;
		} else {
			$this->headers['Transfer-Encoding'] = 'chunked';
			$this->isChunked = true;
		}

		$this->headers['Date'] = gmdate('D, d-M-Y H:i:s T');

		ob_start();
		ob_implicit_flush(false);
		echo $this->protocol, ' ', $this->status, ' ', $this->statusText, "\r\n";
		foreach($this->headers as $name=>$value) {
			if(is_array($value)) {
				foreach($value as $val) {
					echo $name, ': ', (string) $val, "\r\n";
				}
			} else {
				echo $name, ': ', $value, "\r\n";
			}
		}
		echo "\r\n";
		$buf = ob_get_clean();
		
		return $this->send($buf);
	}
	
	public function setContentType(string $type) {
		$this->headers['Content-Type'] = $type;
	}
	
	public function setCookie(string $name, ?string $value, int $expires = 0, string $path = '', string $domain = '', bool $secure = false, bool $httponly = false, string $samesite = '') {
		$this->setRawCookie($name, $value === null ? null : urlencode($value), $expires, $path, $domain, $secure, $httponly, $samesite);
	}
	
	public function setRawCookie(string $name, ?string $value, int $expires = 0, string $path = '', string $domain = '', bool $secure = false, bool $httponly = false, string $samesite = '') {
		if($value === null || $value === '') {
			$expire = gmdate('D, d-M-Y H:i:s T', 0);
			$cookie = "$name=deleted; expires=$expire; Max-Age=0";
		} else {
			$cookie = "$name=$value";
			if($expires > 0) {
				$cookie .= '; expires=' . gmdate('D, d-M-Y H:i:s T', $expires);
				$diff = $expires - time();
				if($diff < 0) $diff = 0;
				$cookie .= "; Max-Age=$diff";
			}
			if($path !== '') $cookie .= "; path=$path";
			if($domain !== '') $cookie .= "; domain=$domain";
			if($secure) $cookie .= "; secure";
			if($httponly) $cookie .= "; HttpOnly";
			if($samesite !== '') $cookie .= "; SameSite=$samesite";
		}
		$this->headers['Set-Cookie'][] = $cookie;
	}
	
	public function write(string $data): bool {
		if(!$this->headSend(-1)) return false;
		
		$n = strlen($data);
		if($n === 0) return true;
		
		if($this->isChunked) {
			$data = sprintf("%x\r\n%s\r\n", $n, $data);
		}
		
		return $this->send($data);
	}
	
	public function end(?string $data = null): bool {
		if($this->isEnd) return true;
		$this->isEnd = true;
		
		$n = strlen($data);
		if(!$this->headSend($n)) return false;
		
		if($this->isChunked) {
			if($n) {
				$data = sprintf("%x\r\n%s\r\n0\r\n\r\n", $n, $data);
			} else {
				$data = "0\r\n\r\n";
			}
		}
		
		return $data === null || $this->send($data);
	}
	
	public function send(string $data): bool {
		loop:
		$n = strlen($data);
		if($n === 0) return true;
		$ret = @socket_send($this->fd, $data, $n, 0);
		if($ret > 0) {
			if($ret < $n) {
				$data = substr($data, $ret);
				goto loop;
			} else return true;
		} else {
			// strerror('socket_send', false);
			socket_clear_error();
			return false;
		}
	}
}

