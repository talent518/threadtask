<?php
namespace app\threadtask;

use yii\base\Component;
use yii\web\Cookie;

class Session extends Component implements \IteratorAggregate, \ArrayAccess, \Countable {
	/**
	 * @var resource
	 */
	private $var, $flash;
	
	public $expire = 7200;
	
	/**
	 *  @var string
	 */
	public $name = 'PHPSESSID';
	
	public $flashParam = '__flash';
	
	private $_id;
	
	public function init() {
		$cookie = \Yii::$app->request->cookies->get($this->name);
		if(!$cookie || !$cookie->value) {
			$this->regenerateID();
		} else {
			$this->setId($cookie->value);
		}
	}
	
	public function open() {
	}
	
	public function getHasSessionId() {
		return \Yii::$app->request->cookies->has($this->name);
	}
	
	public function regenerateID(bool $deleteOldSession=false) {
		if($deleteOldSession) $this->destroy();
		
		$this->setId(md5(random_bytes(16)));
	}
	
	public function getId() {
		return $this->_id;
	}
	
	public function setId($id) {
		if($this->_id && $id !== $this->_id) {
			$this->destroy();
		}
		
		$this->_id = $id;
		
		$cookie = new Cookie();
		$cookie->name = $this->name;
		$cookie->value = $id;
		$cookie->expire = $this->expire + time();
		
		\Yii::$app->response->cookies->add($cookie);
		
		$this->var = ts_var_declare($cookie->value);
		ts_var_expire($this->var, $cookie->expire);
		$this->flash = ts_var_declare($this->flashParam, $this->var);
	}
	
	public function getIsActive() {
		return true;
	}
	
	public function offsetGet($offset) {
		return ts_var_get($this->var, $offset);
	}

	public function getIterator() {
		return new \ArrayIterator(ts_var_get($this->var));
	}

	public function offsetExists($offset) {
		return ts_var_exists($this->var, $offset);
	}

	public function offsetUnset($offset) {
		return ts_var_del($this->var, $offset);
	}

	public function count() {
		return ts_var_count($this->var);
	}
	
	public function getCount() {
		return ts_var_count($this->var);
	}

	public function offsetSet($offset, $value) {
		return ts_var_set($this->var, $offset, $value);
	}
	
	public function has($key) {
		return ts_var_exists($this->var, $key);
	}
	
	public function get($key, $def = null) {
		return ts_var_exists($this->var, $key) ? ts_var_get($this->var, $key) : $def;
	}
	
	public function set($key, $value) {
		return ts_var_set($this->var, $key, $value);
	}
	
	public function remove($key) {
		return ts_var_get($this->var, $key, true);
	}
	
	public function removeAll() {
		ts_var_clean($this->var);
		$this->flash = ts_var_declare($this->flashParam, $this->var);
	}
	
	public function getAllFlashes($delete = false) {
		return ts_var_get($this->flash, null, $delete) ?: [];
	}
	
	public function hasFlash($key) {
		return ts_var_exists($this->flash, $key);
	}
	
	public function getFlash($key, $defaultValue = null, $delete = true) {
		return ts_var_exists($this->flash, $key) ? ts_var_get($this->flash, $key, $delete) : $defaultValue;
	}
	
	public function addFlash($key, $value = true, $removeAfterAccess = true) {
		if(ts_var_exists($this->flash, $key)) {
			return ts_var_inc($this->flash, $key, $value);
		} else {
			return ts_var_set($this->flash, $key, [$value]);
		}
	}
	
	public function setFlash($key, $value = true, $removeAfterAccess = true) {
		return ts_var_set($this->flash, $key, $value);
	}
	
	public function removeFlash($key) {
		return ts_var_get($this->flash, $key, true);
	}
	
	public function destroy() {
		\Yii::$app->response->cookies->remove($this->name);
		$cookie = new Cookie();
		$cookie->name = $this->name;
		$cookie->value = null;
		$cookie->expire = -1;
		\Yii::$app->response->cookies->add($cookie);
		$ret = ts_var_del(ts_var_declare(null), $this->_id);
		$this->_id = null;
		return $ret;
	}

}
