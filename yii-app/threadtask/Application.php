<?php

namespace app\threadtask;

class Application extends \yii\web\Application
{
    public function coreComponents()
    {
        return array_merge(parent::coreComponents(), [
            'request' => ['class' => 'app\threadtask\Request'],
            'response' => ['class' => 'app\threadtask\Response'],
            'session' => ['class' => 'app\threadtask\Session'],
            'errorHandler' => ['class' => 'app\threadtask\ErrorHandler'],
        	'cache' => ['class' => 'app\threadtask\Cache'],
        ]);
    }
    
    /**
     * Terminates the application.
     * This method replaces the `exit()` function by ensuring the application life cycle is completed
     * before terminating the application.
     * @param int $status the exit status (value 0 means normal exit while other values mean abnormal exit).
     * @param Response $response the response to be sent. If not set, the default application [[response]] component will be used.
     * @throws \ExitRequest if the application is in testing mode
     */
    public function end($status = 0, $response = null)
    {
    	if ($this->state === self::STATE_BEFORE_REQUEST || $this->state === self::STATE_HANDLING_REQUEST) {
    		$this->state = self::STATE_AFTER_REQUEST;
    		$this->trigger(self::EVENT_AFTER_REQUEST);
    	}
    	
    	if ($this->state !== self::STATE_SENDING_RESPONSE && $this->state !== self::STATE_END) {
    		$this->state = self::STATE_END;
    		$response = $response ?: $this->getResponse();
    		$response->send();
    	}
    	
   		throw new \ExitRequest();
    }
}
