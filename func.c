#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>

#include <php.h>
#include <php_main.h>
#include <SAPI.h>

#include "func.h"

static sem_t sem;
static pthread_mutex_t lock;
static pthread_cond_t cond;
static volatile unsigned int threads = 0;
static volatile unsigned int delay = 1;
static volatile zend_bool isTry = 1;

void cli_register_file_handles(void);
void sapi_cli_register_variables(char *filename);

void thread_sigmask() {
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	pthread_sigmask(SIG_SETMASK, &set, NULL);
}

void thread_init() {
	sem_init(&sem, 0, 0);
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);

	thread_sigmask();
	cli_register_file_handles();
}

void thread_destroy() {
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&lock);
}

typedef struct _task_t {
	pthread_t thread;
	char *name;
	int argc;
	char **argv;
	struct _task_t *prev;
	struct _task_t *next;
} task_t;

static task_t *head_task = NULL;
static task_t *tail_task = NULL;

void free_task(task_t *task) {
	register int i;

	free(task->name);
	for(i=0; i<task->argc; i++) {
		free(task->argv[i]);
	}
	free(task->argv);
}

void *thread_task(task_t *task) {
	zend_file_handle file_handle;
	sigset_t waitset;
	siginfo_t info;
	struct timespec timeout;

	sigemptyset(&waitset);
	sigaddset(&waitset, SIGINT);
	sigaddset(&waitset, SIGTERM);

	thread_sigmask();

	task->thread = pthread_self();

	pthread_mutex_lock(&lock);
	threads++;
	if(tail_task) {
		tail_task->next = task;
		task->prev = tail_task;
		tail_task = task;
	} else {
		head_task = tail_task = task;
	}
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);

	sem_post(&sem);

	fprintf(stderr, "[%s] begin\n", task->name);

	ts_resource(0);

	SG(options) |= SAPI_OPTION_NO_CHDIR;
	SG(request_info).argc = task->argc;
	SG(request_info).argv = task->argv;
	SG(request_info).path_translated = task->argv[0];

	loop:
	if(php_request_startup() == FAILURE) {
		goto err;
	}

	cli_register_file_handles();

	fprintf(stderr, "[%s] running\n", task->name);

	CG(skip_shebang) = 1;

	zend_first_try {
		zend_stream_init_filename(&file_handle, task->argv[0]);
		php_execute_script(&file_handle);
		fprintf(stderr, "[%s] ok\n", task->name);
	} zend_end_try();

	if(EG(exit_status)) {
		fprintf(stderr, "[%s] exit_status = %d\n", task->name, EG(exit_status));
		php_request_shutdown(NULL);
		timeout.tv_sec = delay;
		timeout.tv_nsec = 0;
		sigprocmask(SIG_BLOCK, &waitset, NULL);
		sigtimedwait(&waitset, &info, &timeout);
		if(isTry) goto loop;
	} else {
		php_request_shutdown(NULL);
	}

	SG(request_info).argc = 0;
	SG(request_info).argv = NULL;

	err:
	fprintf(stderr, "[%s] err\n", task->name);
	ts_free_thread();

	pthread_mutex_lock(&lock);
	threads--;
	if(head_task == task) {
		head_task = head_task->next;
		if(head_task == NULL) {
			tail_task = NULL;
		} else {
			head_task->prev = NULL;
		}
	} else if(tail_task == task) {
		tail_task = tail_task->prev;
		tail_task->next = NULL;
	} else {
		task->prev->next = task->next;
		task->next->prev = task->prev;
	}
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);

	fprintf(stderr, "[%s] end\n", task->name);

	free_task(task);

	pthread_exit(NULL);
}

ZEND_BEGIN_ARG_INFO(arginfo_create_task, 3)
	ZEND_ARG_INFO(0, taskname)
	ZEND_ARG_INFO(0, filename)
	ZEND_ARG_ARRAY_INFO(0, params, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(create_task) {
	zval *params;
	char *taskname, *filename;
	size_t taskname_len, filename_len;
	task_t *task;
	HashTable *ht;
	zend_long idx;
	zend_string *key;
	zval *val;

	pthread_t thread;
	pthread_attr_t attr;
	int ret;

	ZEND_PARSE_PARAMETERS_START(3, 3)
		Z_PARAM_STRING(taskname, taskname_len)
		Z_PARAM_STRING(filename, filename_len)
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

	if(access(filename, F_OK|R_OK) != 0) {
		perror("access() is error");
		RETURN_FALSE;
	}

	ht = Z_ARRVAL_P(params);
	task = (task_t *) malloc(sizeof(task_t));
	memset(task, 0, sizeof(task_t));

	task->name = strndup(taskname, taskname_len);

	task->argc = zend_hash_num_elements(ht) + 1;
	task->argv = (char**) malloc(sizeof(char*) * task->argc);
	task->argv[0] = strndup(filename, filename_len);

	ret = 0;
	ZEND_HASH_FOREACH_KEY_VAL(ht, idx, key, val) {
		convert_to_string(val);
		task->argv[++ret] = strndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
	} ZEND_HASH_FOREACH_END();

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create(&thread, &attr, (void*(*)(void*)) thread_task, task);
	if(ret != 0) {
		perror("pthread_create() is error");
	}
	pthread_attr_destroy(&attr);

	sem_wait(&sem);

	RETURN_BOOL(ret == 0);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_wait, 1)
ZEND_ARG_INFO(0, sig)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_wait) {
	pthread_t thread;
	zend_long sig;
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(sig)
	ZEND_PARSE_PARAMETERS_END();
	isTry = 0;
	pthread_mutex_lock(&lock);
	while(threads > 0) {
		thread = head_task->thread;
		pthread_kill(thread, (int) sig);
		pthread_cond_wait(&cond, &lock);
		pthread_join(thread, NULL);
	}
	pthread_mutex_unlock(&lock);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_set_delay, 1)
ZEND_ARG_INFO(0, delay)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_set_delay) {
	zend_long d;
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(d)
	ZEND_PARSE_PARAMETERS_END();
	RETVAL_LONG(delay);
	delay = d;
}
const zend_function_entry additional_functions[] = {
	ZEND_FE(create_task, arginfo_create_task)
	ZEND_FE(task_wait, arginfo_task_wait)
	ZEND_FE(task_set_delay, arginfo_task_set_delay)
	{NULL, NULL, NULL}
};
