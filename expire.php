<?php
$running = true;
function signal($sig) {
	global $running;

	$running = false;
}

pcntl_async_signals(true);
pcntl_signal(SIGTERM, 'signal', false);
pcntl_signal(SIGINT, 'signal', false);

share_var_init(3);

share_var_put('array', [1,2,3,4]);
share_var_set('array2', [1,2,3,4]);
share_var_set_ex('array3', [1,2,3,4], time()+1);
share_var_set_ex('array4', 'a', 1, time()+1);
share_var_set_ex('array4', 'b', 2, time()+2);
share_var_inc('inc', 1);
share_var_inc('inc', 1);
share_var_inc('inc', 1);
share_var_inc('inc', 1);

$t = time();
for($i=0;$i<26;$i++) share_var_set_ex(chr(ord('a')+$i), $i, $t+$i);

$n = share_var_clean_ex($t);
do {
	var_dump([$t, share_var_get(), $n]); 
	sleep(1);
	$t = time();
	$n = share_var_clean_ex($t);
} while($running && $n > 4);

var_dump([$t, share_var_get(), $n]); 

share_var_set('string', 'abcdefg');
share_var_set('object', new stdClass);

var_dump([share_var_count(), share_var_count('array'), share_var_count('string'), share_var_count('object'), share_var_count('inc')]);

task_wait($exitSig?:SIGINT);

share_var_destory();

