<?php
namespace app\threadtask;

class Cache extends \yii\caching\Cache {
	/**
	 * @var string|resource
	 */
	public $cacheKey = '__ts_cache__';
	
	public function init() {
		$this->cacheKey = ts_var_declare($this->cacheKey);
	}

	public function buildKey($key) {
		return ($_key = $this->isOneArray($key)) !== false ? $_key : ((is_object($key) || is_array($key)) ? md5(serialize($key)) : $key);
	}

	private function isOneArray($array) {
		if(!is_array($array)) {
			return is_object($array) ? false : $array;
		}
		
		$i = 0;
		foreach($array as $key => &$val) {
			if(($i++) !== $key) {
				return false;
			} else {
				$_val = $this->isOneArray($val);
				if($_val === false) {
					return false;
				} elseif(is_array($val)) {
					$val = '[' . $_val . ']';
				} else {
					$val = $_val;
				}
			}
		}
		
		return implode('-', $array);
	}

	public function exists($key) {
		return ts_var_exists($this->cacheKey, $this->buildKey($key));
	}
	
	protected function getValue($key) {
		return ts_var_get($this->cacheKey, $key);
	}
	
	protected function setValue($key, $value, $expire) {
		return ts_var_set($this->cacheKey, $key, $value, $expire > 0 ? $expire + time() : 0);
	}
	
	protected function addValue($key, $value, $expire) {
		return ts_var_exists($this->cacheKey, $key) ? false : ts_var_set($this->cacheKey, $key, $value, $expire > 0 ? $expire + time() : 0);
	}
	
	protected function deleteValue($key) {
		return ts_var_del($this->cacheKey, $key);
	}
	
	protected function flushValues() {
		return ts_var_clean($this->cacheKey) !== false;
	}

}
