<?php

share_var_init(1);

$vars = ['a'=>1, 2];
$a = ['b'=>1];
$vars[] = &$a;

share_var_put($vars);
var_dump(share_var_get());

$a = &$vars;
$b = &$a;

$vars['vars'] = &$b;

share_var_put($vars);
var_dump(share_var_get());

share_var_destory();

