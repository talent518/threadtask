<?php

use yii\helpers\Html;
use yii\widgets\DetailView;

/** @var yii\web\View $this */
/** @var app\models\User $model */

$this->title = $model->username;
$this->params['breadcrumbs'][] = ['label' => '用户管理', 'url' => ['index']];
$this->params['breadcrumbs'][] = $this->title;
\yii\web\YiiAsset::register($this);
?>
<div class="user-view">

    <h1><?= Html::encode($this->title) ?></h1>

    <p>
        <?= Html::a('更新用户', ['update', 'id' => $model->uid], ['class' => 'btn btn-primary']) ?>
        <?= Html::a('删除用户', ['delete', 'id' => $model->uid], [
            'class' => 'btn btn-danger',
            'data' => [
                'confirm' => '你确定要删除“' . $model->username . '”用户吗？',
                'method' => 'post',
            ],
        ]) ?>
    </p>

    <?= DetailView::widget([
        'model' => $model,
        'attributes' => [
            'uid',
            'username',
            'email:email',
            'registerTime',
            'loginTime',
            'loginTimes',
        ],
    ]) ?>

</div>
