<?php
use yii\base\ExitException;

// defined('YII_DEBUG') or define('YII_DEBUG', true);
// defined('YII_ENV') or define('YII_ENV', 'dev');

require __DIR__ . '/vendor/autoload.php';
require __DIR__ . '/vendor/yiisoft/yii2/Yii.php';
require __DIR__ . '/threadtask/Application.php';

$config = require __DIR__ . '/config/threadtask.php';

$_SERVER['SCRIPT_FILENAME'] = __DIR__ . '/web/index.php';
$_SERVER['SCRIPT_NAME'] = '/index.php';

$onBody = function(HttpRequest $request) {
	return true; // return onBody($request);
};

$onRequest = function(HttpRequest $request, HttpResponse $response) use(&$app, $config) { // return onRequest($request, $response);
	static $db;
	
	if($request->path !== '/' && $request->path !== '.htaccess' && substr($request->path, -4) !== '.php') {
		$path = __DIR__ . '/web/' . $request->path;
		if(file_exists($path)) return onMediaFile($request, $response, $path);
	}
	
	try {
		$config['components']['request']['request'] = $request;
		$config['components']['response']['response'] = $response;
		
		$displayErrors = ini_set('display_errors', false);
		set_error_handler(function($code, $message, $file, $line) {
			if(error_reporting() & $code) throw new ErrorException($message, $code, $code, $file, $line);
			return false;
		});
		
// 		if(Yii::$app) {
// 			Yii::$app->set('request', $config['components']['request']);
// 			Yii::$app->set('response', $config['components']['response']);
// 			Yii::$app->init();
// 			Yii::$app->run();
// 		} else {
			if($db) $config['components']['db'] = $db;
			(new app\threadtask\Application($config))->run();
// 		}
	} catch(ExitException $e) {
	} catch(\ExitRequest $e) {
		return $e->getMessage();
	} catch(\Throwable $e) {
		if(\Yii::$app && ($errorHandler = \Yii::$app->get('errorHandler', false))) {
			$errorHandler->handleException($e);
			return null;
		}
		
		$response->status = 500;
		$response->statusText = 'Internal Server Error';
		$response->setContentType('text/plain; charset=utf-8');
		if(YII_DEBUG) {
			return (string) $e;
		} else {
			echo "$e\n";
			return 'An internal server error occurred.';
		}
	} finally {
// 		unset($config['components']['request']['request'], $config['components']['response']);

// 		Yii::$app->set('request', $config['components']['request']);
// 		Yii::$app->set('response', $config['components']['response']);
// 		Yii::$app->set('session', $config['components']['session']);

		if(!$db && Yii::$app && Yii::$app->has('db', true)) {
			$db = Yii::$app->getDb();
		}
		
		call_and_free_shutdown();
		error_clear_last();
		Yii::$app = null;
		
		restore_error_handler();
		ini_set('display_errors', $displayErrors);
	}
	return null;
};

class ExitRequest extends \Exception {
}
