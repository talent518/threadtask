<?php

namespace app\models;


use yii\db\Expression;

/**
 * This is the model class for table "user".
 *
 * @property int $uid
 * @property string $username
 * @property string $email
 * @property string $password
 * @property string $salt
 * @property string $registerTime
 * @property string|null $loginTime
 * @property int $loginTimes
 */
class User extends \yii\db\ActiveRecord implements \yii\web\IdentityInterface
{
    /**
     * {@inheritdoc}
     */
    public static function tableName()
    {
        return 'user';
    }

    /**
     * {@inheritdoc}
     */
    public function rules()
    {
        return [
            [['username', 'email', 'password'], 'required'],
            [['registerTime', 'loginTime'], 'safe'],
            [['loginTimes'], 'integer'],
            [['username'], 'string', 'max' => 20],
            [['email'], 'string', 'max' => 100],
        	[['email'], 'email'],
            [['password'], 'string', 'max' => 32],
            [['username'], 'unique'],
            [['email'], 'unique'],
        ];
    }

    /**
     * {@inheritdoc}
     */
    public function attributeLabels()
    {
        return [
            'uid' => '用户ID',
            'username' => '用户名',
            'email' => '邮箱',
            'password' => '密码',
            'salt' => '安全码',
            'registerTime' => '注册时间',
            'loginTime' => '登录时间',
            'loginTimes' => '登录次数',
        ];
    }

    private $_password;
    public function afterFind() {
    	parent::afterFind();
    	$this->_password = $this->password;
    }
	
    public function beforeSave($insert) {
    	if($insert || $this->_password !== $this->password) {
    		$this->salt = bin2hex(random_bytes(4));
    		$this->password = md5(md5($this->password) . $this->salt);
    	}
    	if($insert) {
    		$this->registerTime = new Expression('NOW()');
    	}
    	
    	return parent::beforeSave($insert);
    }
	
	public function getId() {
		return $this->uid;
	}

	public function validateAuthKey($authKey) {
		return $authKey === $this->getAuthKey();
	}

	public function getAuthKey() {
		return md5($this->uid . $this->password . $this->salt);
	}
	
	public function validatePassword($password) {
		return $this->password === md5(md5($password) . $this->salt);
	}

	public static function findIdentity($id) {
		return self::findOne($id);
	}

	public static function findIdentityByAccessToken($token, $type = null) {
	}
	
	public static function findByUsername($username) {
		return self::findOne(compact('username'));
	}

}
