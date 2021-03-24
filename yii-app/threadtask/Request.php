<?php
namespace app\threadtask;

use yii\base\InvalidConfigException;
use yii\web\HeaderCollection;
use yii\web\NotFoundHttpException;
use yii\web\RequestParserInterface;

/**
 * @inheritdoc
 */
class Request extends \yii\web\Request {

	/**
	 * @var HeaderCollection Collection of request headers.
	 */
	private $_headers;
	
	/**
	 * @var string toupper
	 */
	private $_method;
	
	/**
	 * @var \HttpRequest
	 */
	private $_request;
	
	public function setRequest(\HttpRequest $request) {
		$this->_request = $request;
	}
	
	public function getRequest() {
		return $this->_request;
	}
	
	/**
	 * @inheritdoc
	 */
	public function resolve()
	{
		$result = \Yii::$app->getUrlManager()->parseRequest($this);
		if ($result !== false) {
			list($route, $params) = $result;
			if ($this->_queryParams === null) {
				$this->_request->get = $params + $this->_request->get; // preserve numeric keys
			} else {
				$this->_queryParams = $params + $this->_queryParams;
			}
			
			return [$route, $this->getQueryParams()];
		}
		
		throw new NotFoundHttpException(\Yii::t('yii', 'Page not found.'));
	}
	
	/**
	 * @inheritdoc
	 */
	public function getHeaders()
	{
		if ($this->_headers === null) {
			$this->_headers = new HeaderCollection();
			foreach ($this->_request->headers as $name => $value) {
				$this->_headers->add($name, $value);
			}
			$this->filterHeaders($this->_headers);
		}
		
		return $this->_headers;
	}
	
	/**
	 * @inheritdoc
	 */
	public function getMethod()
	{
		if($this->_method !== null) return $this->_method;
		
		if (isset($this->_request->post[$this->methodParam]) && !in_array(strtoupper($this->_request->post[$this->methodParam]), ['GET', 'HEAD', 'OPTIONS'], true)) {
			return $this->_method = strtoupper($this->_request->post[$this->methodParam]);
		}

		if ($this->headers->has('X-Http-Method-Override')) {
			return $this->_method = strtoupper($this->headers->get('X-Http-Method-Override'));
		}
		
		return $this->_method = strtoupper($this->_request->method);
	}
	
	private $_rawBody;
	
	/**
	 * Returns the raw HTTP request body.
	 * @return string the request body
	 */
	public function getRawBody()
	{
		if ($this->_rawBody === null) {
			$this->_rawBody = &$this->_request->buf;
		}
		
		return $this->_rawBody;
	}
	
	/**
	 * Sets the raw HTTP request body, this method is mainly used by test scripts to simulate raw HTTP requests.
	 * @param string $rawBody the request body
	 */
	public function setRawBody($rawBody)
	{
		$this->_rawBody = $rawBody;
	}
	
	private $_bodyParams;
	
	/**
	 * @inheritdoc
	 */
	public function getBodyParams()
	{
		if ($this->_bodyParams === null) {
			if (isset($this->_request->post[$this->methodParam])) {
				$this->_bodyParams = $this->_request->post;
				unset($this->_bodyParams[$this->methodParam]);
				return $this->_bodyParams;
			}
			
			$rawContentType = $this->getContentType();
			if (($pos = strpos($rawContentType, ';')) !== false) {
				// e.g. text/html; charset=UTF-8
				$contentType = substr($rawContentType, 0, $pos);
			} else {
				$contentType = $rawContentType;
			}
			
			if (isset($this->parsers[$contentType])) {
				$parser = \Yii::createObject($this->parsers[$contentType]);
				if (!($parser instanceof RequestParserInterface)) {
					throw new InvalidConfigException("The '$contentType' request parser is invalid. It must implement the yii\\web\\RequestParserInterface.");
				}
				$this->_bodyParams = $parser->parse($this->getRawBody(), $rawContentType);
			} elseif (isset($this->parsers['*'])) {
				$parser = \Yii::createObject($this->parsers['*']);
				if (!($parser instanceof RequestParserInterface)) {
					throw new InvalidConfigException('The fallback request parser is invalid. It must implement the yii\\web\\RequestParserInterface.');
				}
				$this->_bodyParams = $parser->parse($this->getRawBody(), $rawContentType);
			} elseif ($this->getMethod() === 'POST') {
				// PHP has already parsed the body so we have all params in $_POST
				$this->_bodyParams = $this->_request->post;
			} else {
				$this->_bodyParams = [];
				mb_parse_str($this->getRawBody(), $this->_bodyParams);
			}
		}
		
		return $this->_bodyParams;
	}
	
	/**
	 * @inheritdoc
	 */
	public function setBodyParams($values)
	{
		$this->_bodyParams = $values;
	}
	
	private $_queryParams;
	
	/**
	 * @inheritdoc
	 */
	public function getQueryParams()
	{
		if ($this->_queryParams === null) {
			return $this->_request->get;
		}
		
		return $this->_queryParams;
	}
	
	/**
	 * @inheritdoc
	 */
	public function setQueryParams($values)
	{
		$this->_queryParams = $values;
	}
	
	/**
	 * @inheritdoc
	 */
	protected function resolveRequestUri()
	{
		if ($this->headers->has('X-Rewrite-Url')) { // IIS
			return $this->headers->get('X-Rewrite-Url');
		} else {
			return $this->_request->uri;
		}
	}
	
	/**
	 * @inheritdoc
	 */
	public function getQueryString()
	{
		return $this->_request->query;
	}
	
	/**
	 * @inheritdoc
	 */
	public function getRemoteIP()
	{
		return $this->_request->clientAddr;
	}
	
	/**
	 * @inheritdoc
	 */
	public function getRemoteHost()
	{
		return $this->_request->clientAddr;
	}
	
	/**
	 * @inheritdoc
	 */
	public function getAuthCredentials()
	{
		$auth_token = $this->getHeaders()->get('Authorization');
		if ($auth_token !== null && strncasecmp($auth_token, 'basic', 5) === 0) {
			$parts = array_map(function ($value) {
				return strlen($value) === 0 ? null : $value;
			}, explode(':', base64_decode(mb_substr($auth_token, 6)), 2));
				
				if (count($parts) < 2) {
					return [$parts[0], null];
				}
				
				return $parts;
		}
		
		return [null, null];
	}
	
	
	/**
	 * @inheritdoc
	 */
	protected function loadCookies()
	{
		$cookies = [];
		if ($this->enableCookieValidation) {
			if ($this->cookieValidationKey == '') {
				throw new InvalidConfigException(get_class($this) . '::cookieValidationKey must be configured with a secret key.');
			}
			foreach ($this->_request->cookies as $name => $value) {
				if (!is_string($value)) {
					continue;
				}
				$data = \Yii::$app->getSecurity()->validateData($value, $this->cookieValidationKey);
				if ($data === false) {
					continue;
				}
				if (defined('PHP_VERSION_ID') && PHP_VERSION_ID >= 70000) {
					$data = @unserialize($data, ['allowed_classes' => false]);
				} else {
					$data = @unserialize($data);
				}
				if (is_array($data) && isset($data[0], $data[1]) && $data[0] === $name) {
					$cookies[$name] = \Yii::createObject([
						'class' => 'yii\web\Cookie',
						'name' => $name,
						'value' => $data[1],
						'expire' => null,
					]);
				}
			}
		} else {
			foreach ($this->_request->cookies as $name => $value) {
				$cookies[$name] = \Yii::createObject([
					'class' => 'yii\web\Cookie',
					'name' => $name,
					'value' => $value,
					'expire' => null,
				]);
			}
		}
		
		return $cookies;
	}

}