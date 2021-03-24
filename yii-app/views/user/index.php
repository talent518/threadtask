<?php

use yii\helpers\Html;
use yii\grid\GridView;
use yii\widgets\Pjax;
/* @var $this yii\web\View */
/* @var $searchModel app\models\UserSearch */
/* @var $dataProvider yii\data\ActiveDataProvider */

$this->title = '用户管理';
$this->params['breadcrumbs'][] = $this->title;
?>
<div class="user-index">

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

        	['class' => 'yii\grid\ActionColumn', 'contentOptions'=>['class'=>'text-nowrap text-center']],
        ],
    ]); ?>

    <?php Pjax::end(); ?>

</div>
