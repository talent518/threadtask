<?php
namespace app\threadtask;

use yii\helpers\VarDumper;

class FileTarget extends \yii\log\FileTarget {
	protected function getContextMessage() {
		return VarDumper::dumpAsString(\Yii::$app->getRequest()->request);
	}
}
