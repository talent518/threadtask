<?php
$t = microtime(true);

share_var_init(1);

create_task('cmd1', __DIR__ . '/cmd.php', ['return', 'v0'], null, null, $res1) or die("create task failure\n");
create_task('cmd2', __DIR__ . '/cmd.php', ['return', 'v1'], null, null, $res2) or die("create task failure\n");

task_kill($res1);
task_join($res2);
$v0 = share_var_get_and_del('v0');
$v1 = share_var_get_and_del('v1');
echo "V0: {$v0}ms\n";
echo "V1: {$v1}ms\n";

$t0 = (int)((microtime(true) - $t) * 1000000) / 1000;
echo "T1: {$t0}ms\n";

$res = NULL;

task_wait(SIGINT);

$t0 = (int)((microtime(true) - $t) * 1000000) / 1000;
echo "T2: {$t0}ms\n";

share_var_destory();
