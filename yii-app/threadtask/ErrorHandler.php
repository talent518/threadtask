<?php
namespace app\threadtask;

use Yii;
use yii\helpers\VarDumper;
use yii\web\Response;

class ErrorHandler extends \yii\web\ErrorHandler {

	public function register() {
	}
	
	public function unregister() {
	}
	
	/**
	 * @inheritdoc
	 */
	public function handleException($exception)
	{
		$this->exception = $exception;
		
		try {
			$this->logException($exception);
			if ($this->discardExistingOutput) {
				$this->clearOutput();
			}
			$this->renderException($exception);
		} catch (\Exception $e) {
			// an other exception could be thrown while displaying the exception
			$this->handleFallbackExceptionMessage($e, $exception);
		} catch (\Throwable $e) {
			// additional check for \Throwable introduced in PHP 7
			$this->handleFallbackExceptionMessage($e, $exception);
		}
		
		$this->exception = null;
	}
	
	/**
	 * @inheritdoc
	 */
	protected function handleFallbackExceptionMessage($exception, $previousException)
	{
		$msg = "An Error occurred while handling another error:\n";
		$msg .= (string) $exception;
		$msg .= "\nPrevious exception:\n";
		$msg .= (string) $previousException;
		
		$response = \Yii::$app->getResponse();
		$request = VarDumper::export(\Yii::$app->getRequest()->request);
		if (YII_DEBUG) {
			$msg .= "\n\$request = $request";
			if($response->isSent) {
				echo "\n$msg\n";
			} else {
				$response->data = $response->format === Response::FORMAT_HTML ? '<pre>' . htmlspecialchars($msg, ENT_QUOTES, Yii::$app->charset) . '</pre>' : $msg;
				$response->send();
			}
		} else {
			if($response->isSent) {
				echo "\n$msg\n\$request = $request\n";
			} else {
				$response->data = 'An internal server error occurred.';
				$response->send();
			}
		}
		error_log($msg);
	}

	public function renderRequest() {
		return '<pre>' . $this->htmlEncode(VarDumper::dump(\Yii::$app->getRequest()->request, 4, true)) . '</pre>';
	}

}
