<?php
share_var_init();

$var = ts_var_declare('VAR');
debug('var', $var);

$var1 = ts_var_declare('str', $var);
debug('var1', $var1);

$var2 = ts_var_declare(0, $var, true);
debug('var2', $var2);

$fd0 = ts_var_fd($var2, true);
debug('fd0', $fd0);
debug('fd0.1', @socket_write($fd0, '1', 1));
debug('fd0.2', @socket_write($fd0, '2', 1));
debug('fd0.3', @socket_write($fd0, '3', 1));

$fd1 = ts_var_fd($var2, false);
debug('fd1', $fd1);
debug('fd1.123', @socket_read($fd1, 3));

$ex0 = ts_var_exists($var, 'str');
debug('ex0', $ex0);

$ex1 = ts_var_exists($var, 0);
debug('ex1', $ex1);

debug('vars', share_var_get());

debug('set.var1.str', ts_var_set($var1, 'str', __FILE__));
debug('set.var2.0', ts_var_set($var2, 0, 123));

debug('vars', share_var_get());

debug('set.var.str', ts_var_set($var, 'str', __FILE__));
debug('set.var.0', ts_var_set($var, 0, 123));

debug('vars', share_var_get());

debug('set.var1.str', ts_var_set($var1, 'str2', __FILE__));
debug('set.var2.0', ts_var_set($var2, 1, 123));

debug('set.var1.null.0', ts_var_set($var1, null, __FILE__));
debug('set.var2.null.1', ts_var_set($var2, null, 123.45));

debug('set.var.null.0', ts_var_set($var, null, __FILE__));
debug('set.var.null.1', ts_var_set($var, null, 123.45));

debug('get.var', ts_var_get($var));
debug('get.var1.str', ts_var_get($var1, 'str', true));
debug('get.var1.str2', ts_var_get($var1, 'str2'));
debug('get.var1', ts_var_get($var1));
debug('get.var2.0', ts_var_get($var2, 0, true));
debug('get.var2.1', ts_var_get($var2, 1));
debug('get.var2', ts_var_get($var2));

debug('del.var1.str2', ts_var_del($var1, 'str2'));
debug('get.var1', ts_var_get($var1));
debug('del.var2.1', ts_var_del($var2, 1));
debug('get.var2', ts_var_get($var2));

for($i=0; $i<5; $i++) {
	debug('inc.var.str', ts_var_inc($var, 'str', 1));
	debug('inc.var.0', ts_var_inc($var, 0, -1));
	debug('inc.var.null.0', ts_var_inc($var, null, $i));
	debug('inc.var.null.1', ts_var_inc($var, null, chr(ord('A')+$i)));
	debug('inc.var.chr', ts_var_inc($var, 'chr', chr(ord('A')+$i)));
}

debug('set.var.expire', ts_var_set($var, 'expire', 1, 1));
debug('get.var', ts_var_get($var));
debug('clean.var', ts_var_clean($var, time()));
debug('count.var', ts_var_count($var));

debug('vars', share_var_get());

debug('push.var', ts_var_push($var, 'first', 1, 2, 3, 'last'));
debug('get.var', ts_var_get($var));
debug('pop.var', ts_var_pop($var));
debug('shift.var', ts_var_shift($var));
debug('get.var', ts_var_get($var));
debug('set.var', ts_var_set($var, 'a', 'a'));
debug('get.var', ts_var_get($var));
debug('reindex.var', ts_var_reindex($var));
debug('get.var', ts_var_get($var));

share_var_destory();

function debug($name, $var) {
	echo "$name: ";
	var_dump($var);
	echo "\n";
}

