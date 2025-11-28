<?php

use app\models\User;
use yii\helpers\Html;
use yii\helpers\Url;
use yii\grid\ActionColumn;
use yii\grid\GridView;
use yii\widgets\Pjax;

/** @var yii\web\View $this */
/** @var app\models\UserSearch $searchModel */
/** @var yii\data\ActiveDataProvider $dataProvider */

$this->title = '用户管理';
$this->params['breadcrumbs'][] = $this->title;
?>
<div class="user-index">

    <h1><?= Html::encode($this->title) ?></h1>

    <p>
        <?= Html::a('新增用户', ['create'], ['class' => 'btn btn-success']) ?>
    </p>

    <?php Pjax::begin(); ?>
    <?php // echo $this->render('_search', ['model' => $searchModel]); ?>

    <?= GridView::widget([
        'dataProvider' => $dataProvider,
        'filterModel' => $searchModel,
        'columns' => [
            ['class' => 'yii\grid\SerialColumn'],

        	['attribute' => 'uid', 'contentOptions'=>['width'=>'76px','class'=>'text-center']],
            'username',
            'email:email',
            'registerTime',
            'loginTime',
        	['attribute'=>'loginTimes', 'contentOptions'=>['width'=>'76px','class'=>'text-center']],
            [
                'class' => ActionColumn::class,
                'contentOptions'=>['class'=>'text-nowrap text-center'],
                'urlCreator' => function ($action, User $model, $key, $index, $column) {
                    return Url::toRoute([$action, 'id' => $model->uid]);
                 }
            ],
        ],
    ]); ?>

    <?php Pjax::end(); ?>

</div>
