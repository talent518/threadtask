<?php
$t = microtime(true);

share_var_init(1);

create_task('cmd1', __DIR__ . '/cmd.php', ['return'], null, null, $res) or die("create task failure\n");
create_task('cmd2', __DIR__ . '/cmd.php', []) or die("create task failure\n");

task_join($res);
$t0 = share_var_get('return');
share_var_del('return');
echo "T0: $t0\n";

$t0 = microtime(true) - $t;
echo "T1: $t0\n";

$res = NULL;

task_wait(SIGINT);

$t0 = microtime(true) - $t;
echo "T2: $t0\n";

share_var_destory();

