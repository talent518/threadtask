<?php

$var = ts_var_declare(null);
$var1 = ts_var_declare(1);
$var2 = ts_var_declare(2);

var_dump(ts_var_get_or_set($var1, 1, function() {
	echo 'call: ', __LINE__, PHP_EOL;
	return __LINE__;
}, 1));

var_dump(ts_var_get($var));

echo PHP_EOL;

var_dump(ts_var_get_or_set($var1, 'a', function() {
	echo 'call: ', __LINE__, PHP_EOL;
	return __LINE__;
}, 1));

var_dump(ts_var_get($var));

echo PHP_EOL;

var_dump(ts_var_get_or_set($var2, 1, function() use(&$var1) {
	echo 'call: ', __LINE__, PHP_EOL;
	return ts_var_get($var1);
}, 1));

var_dump(ts_var_get($var));

echo PHP_EOL;

var_dump(ts_var_get_or_set($var2, 1, function() use(&$var1) {
	echo 'call: ', __LINE__, PHP_EOL;
	return ts_var_get($var1);
}, 1));

var_dump(ts_var_get($var));

echo PHP_EOL;

var_dump(ts_var_get_or_set($var2, 'a', function() use(&$var2) {
	echo 'call: ', __LINE__, PHP_EOL;
	return ts_var_get($var2);
}, 1));

var_dump(ts_var_get($var));

echo PHP_EOL;

var_dump(ts_var_get_or_set($var2, 'a', function() use(&$var2) {
	echo 'call: ', __LINE__, PHP_EOL;
	return ts_var_get($var2);
}, 1));

var_dump(ts_var_get($var));

echo PHP_EOL;

var_dump(ts_var_get_or_set($var2, 'b', function() use(&$var2) {
	echo 'call: ', __LINE__, PHP_EOL;
	return ts_var_set($var2, 'c', true);
}, 1));

var_dump(ts_var_get($var));

