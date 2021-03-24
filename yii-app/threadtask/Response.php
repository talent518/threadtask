<?php
namespace app\threadtask;

use yii\base\InvalidConfigException;

class Response extends \yii\web\Response {

	/**
	 *
	 * @var \HttpResponse
	 */
	private $_response;


	public function setResponse(\HttpResponse $response) {
		$this->_response = $response;
	}

	/**
	 * Sends the response headers to the client.
	 */
	protected function sendHeaders() {
		foreach($this->getHeaders() as $name => $values) {
			$name = str_replace(' ', '-', ucwords(str_replace('-', ' ', $name)));
			$this->_response->headers[$name] = $values;
		}

		$this->_response->protocol = "HTTP/{$this->version}";
		$this->_response->status = $this->getStatusCode();
		$this->_response->statusText = $this->statusText;
		$this->sendCookies();
	}

	/**
	 * Sends the cookies to the client.
	 */
	protected function sendCookies() {
		$cookies = $this->getCookies();
		if($cookies->getCount() === 0) {
			return;
		}
		$request = \Yii::$app->getRequest();
		if($request->enableCookieValidation) {
			if($request->cookieValidationKey == '') {
				throw new InvalidConfigException(get_class($request) . '::cookieValidationKey must be configured with a secret key.');
			}
			$validationKey = $request->cookieValidationKey;
		}
		foreach($cookies as $cookie) {
			$value = $cookie->value;
			if($cookie->expire != 1 && isset($validationKey)) {
				$value = \Yii::$app->getSecurity()->hashData(serialize([
					$cookie->name,
					$value
				]), $validationKey);
			}
			$this->_response->setCookie($cookie->name, $value, $cookie->expire, $cookie->path, $cookie->domain, $cookie->secure, $cookie->httpOnly, $cookie->sameSite);
		}
	}

	/**
	 * Sends the response content to the client.
	 */
	protected function sendContent() {
		if($this->stream === null) {
			$this->_response->end($this->content);
			return;
		}

		// Try to reset time limit for big files
		if(! function_exists('set_time_limit') || ! @set_time_limit(0)) {
			\Yii::warning('set_time_limit() is not available', __METHOD__);
		}

		if(is_callable($this->stream)) {
			$data = call_user_func($this->stream);
			foreach($data as $datum) {
				echo $datum;
				flush();
			}
			return;
		}

		$chunkSize = 8 * 1024 * 1024; // 8MB per chunk

		if(is_array($this->stream)) {
			list($handle, $begin, $end) = $this->stream;
			
			$this->_response->headSend($end - $begin + 1);

			// only seek if stream is seekable
			if($this->isSeekable($handle)) {
				fseek($handle, $begin);
			}

			while(! feof($handle) && ($pos = ftell($handle)) <= $end) {
				if($pos + $chunkSize > $end) {
					$chunkSize = $end - $pos + 1;
				}
				$this->_response->send(fread($handle, $chunkSize));
			}
			fclose($handle);
		} else {
			if($this->_response->headers['Content-Length'] > 0) {
				$this->_response->headSend((int) $this->_response->headers['Content-Length']);
			} elseif($this->isSeekable($this->stream)) {
				$begin = ftell($this->stream);
				fseek($this->stream, 0, SEEK_END);
				$this->_response->headSend(ftell($this->stream) - $begin + 1);
				fseek($this->stream, $begin);
			} else {
				$this->_response->headSend(-1);
			}
			while(! feof($this->stream)) {
				$this->_response->send(fread($this->stream, $chunkSize));
			}
			fclose($this->stream);
		}
	}

	/**
	 * Checks if a stream is seekable
	 *
	 * @param
	 *        	$handle
	 * @return bool
	 */
	private function isSeekable($handle) {
		if(! is_resource($handle)) {
			return true;
		}

		$metaData = stream_get_meta_data($handle);
		return isset($metaData['seekable']) && $metaData['seekable'] === true;
	}
}