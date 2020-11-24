#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <limits.h>
#include <time.h>

#include <php.h>
#include <php_main.h>
#include <SAPI.h>
#include <php_output.h>

#include "func.h"

static sem_t sem, rsem;
static pthread_mutex_t nlock, wlock;
static pthread_cond_t ncond;
static volatile unsigned int maxthreads = 256;
static volatile unsigned int threads = 0;
static volatile unsigned int wthreads = 0;
static volatile unsigned int delay = 1;
static volatile zend_bool isRun = 1;
static pthread_t mthread;
static pthread_key_t pkey;

volatile zend_bool isDebug = 1;
volatile zend_bool isReload = 0;

#define SINFO(key) ((server_info_t*) SG(server_context))->key

typedef struct _server_info_t {
	char strftime[20];
} server_info_t;

static server_info_t main_sinfo;

typedef struct _task_t {
	pthread_t thread;
	char *name;
	int argc;
	char **argv;
	char *logfile;
	char *logmode;
	FILE *fp;
	struct _task_t *prev;
	struct _task_t *next;
} task_t;

static task_t *taskn = NULL;
static task_t *head_task = NULL;
static task_t *tail_task = NULL;

const char *gettimeofstr() {
	time_t t;
	struct tm *tmp;

	t = time(NULL);
	tmp = localtime(&t);
	if (tmp == NULL) {
		perror("localtime error");
		return "";
	}

	if (strftime(SINFO(strftime), sizeof(SINFO(strftime)), "%F %T", tmp) == 0) {
		perror("strftime error");
		return "";
	}

	return SINFO(strftime);
}

void free_task(task_t *task) {
	register int i;

	free(task->name);
	for(i=0; i<task->argc; i++) {
		free(task->argv[i]);
	}
	free(task->argv);

	if(task->logfile) free(task->logfile);
	if(task->logmode) free(task->logmode);
	if(task->fp) fclose(task->fp);

	
	free(task);
}

void thread_sigmask() {
	register int sig;
	sigset_t set;

	sigemptyset(&set);

	for(sig=SIGHUP; sig<=SIGSYS; sig++) {
		sigaddset(&set, sig);
	}

	pthread_sigmask(SIG_SETMASK, &set, NULL);
}

void thread_init() {
	sem_init(&sem, 0, 0);
	sem_init(&rsem, 0, 0);
	pthread_mutex_init(&nlock, NULL);
	pthread_mutex_init(&wlock, NULL);
	pthread_cond_init(&ncond, NULL);
	pthread_key_create(&pkey, NULL);
	pthread_setspecific(pkey, NULL);

	thread_sigmask();

	mthread = pthread_self();
}

void thread_running() {
	SG(server_context) = &main_sinfo;
}

void thread_destroy() {
	if(taskn) {
		free_task(taskn);
		taskn = NULL;
	}
	
	pthread_key_delete(pkey);
	pthread_cond_destroy(&ncond);
	pthread_mutex_destroy(&nlock);
	pthread_mutex_destroy(&wlock);
	sem_destroy(&rsem);
	sem_destroy(&sem);
}

size_t (*old_ub_write_handler)(const char *str, size_t str_length);
void (*old_flush_handler)(void *server_context);

size_t php_thread_ub_write_handler(const char *str, size_t str_length) {
	task_t *task = (task_t *) pthread_getspecific(pkey);

	if(task == NULL) {
		return old_ub_write_handler(str, str_length);
	}

	try:
	if(task->fp == NULL) {
		task->fp = fopen(task->logfile, task->logmode);
		if(task->fp == NULL) {
			dprintf("[%s] open file %s is failure, code is %d, error is %s\n", task->name, task->logfile, errno, strerror(errno));
			return FAILURE;
		}
	}

	if(fwrite(str, 1, str_length, task->fp) != str_length) {
		if(errno == EACCES) {
			dprintf("[%s] write file %s is failure, code is %d, error is %s\n", task->name, task->logfile, errno, strerror(errno));
			return old_ub_write_handler(str, str_length);
		} else {
			fclose(task->fp);
			task->fp = NULL;
			goto try;
		}
	}

	return str_length;
}

void php_thread_flush_handler(void *server_context) {
	task_t *task = (task_t *) pthread_getspecific(pkey);

	if(task == NULL) {
		old_flush_handler(server_context);
	} else if(task->fp) {
		if(fflush(task->fp) == EOF) {
			fclose(task->fp);
			task->fp = NULL;
		}
	}
}

void cli_register_file_handles(void);

void *thread_task(task_t *task) {
	zend_file_handle file_handle;
	sigset_t waitset;
	siginfo_t info;
	struct timespec timeout;
	char path[PATH_MAX];
	server_info_t sinfo;

	sigemptyset(&waitset);
	sigaddset(&waitset, SIGINT);
	sigaddset(&waitset, SIGTERM);

	thread_sigmask();

	task->thread = pthread_self();

	pthread_mutex_lock(&nlock);
	if(tail_task) {
		tail_task->next = task;
		task->prev = tail_task;
		tail_task = task;
	} else {
		head_task = tail_task = task;
	}
	pthread_mutex_unlock(&nlock);

	sem_post(&sem);

	ts_resource(0);

	SG(server_context) = &sinfo;

	dprintf("begin thread\n");

newtask:
	dprintf("[%s] newtask\n", task->name);
	
	if(task->logfile && task->logmode) {
		pthread_setspecific(pkey, task);
	} else {
		pthread_setspecific(pkey, NULL);
	}
	
	SG(options) |= SAPI_OPTION_NO_CHDIR;
	SG(request_info).argc = task->argc;
	SG(request_info).argv = task->argv;
	SG(request_info).path_translated = task->argv[0];

	loop:
	if(php_request_startup() == FAILURE) {
		goto err;
	}

	thread_sigmask();

	SG(headers_sent) = 1;
	SG(request_info).no_headers = 1;

	zend_register_string_constant(ZEND_STRL("THREAD_TASK_NAME"), task->name, CONST_CS, PHP_USER_CONSTANT);

	cli_register_file_handles();

	dprintf("[%s] running\n", task->name);

	CG(skip_shebang) = 1;

	zend_first_try {
		if(realpath(task->argv[0], path) == NULL) {
			dprintf("[%s] %d %s\n", task->name, errno, strerror(errno));
		} else {
			zend_stream_init_filename(&file_handle, path);
			php_execute_script(&file_handle);
			dprintf("[%s] oktask\n", task->name);
		}
	} zend_end_try();

	if(EG(exit_status)) {
		dprintf("[%s] exit_status = %d\n", task->name, EG(exit_status));
		php_request_shutdown(NULL);
		timeout.tv_sec = delay;
		timeout.tv_nsec = 0;
		sigprocmask(SIG_BLOCK, &waitset, NULL);
		sigtimedwait(&waitset, &info, &timeout);
		if(isRun) goto loop;
	} else {
		php_request_shutdown(NULL);
	}

	SG(request_info).argc = 0;
	SG(request_info).argv = NULL;
	
	if(!isRun) goto err;
	
	dprintf("[%s] waittask\n", task->name);
	
	pthread_mutex_lock(&wlock);
	wthreads++;
	pthread_mutex_unlock(&wlock);

	if(!clock_gettime(CLOCK_REALTIME, &timeout)) {
		timeout.tv_sec += delay;
		timeout.tv_nsec = 0;
		sem_timedwait(&rsem, &timeout);
	} else sem_wait(&rsem);

	pthread_mutex_lock(&wlock);
	wthreads--;
	if(taskn) {
		pthread_mutex_lock(&nlock);
		taskn->thread = task->thread;
		taskn->prev = task->prev;
		taskn->next = task->next;
		if(taskn->prev) {
			taskn->prev->next = taskn;
		} else {
			head_task = taskn;
		}
		if(taskn->next) {
			taskn->next->prev = taskn;
		} else {
			tail_task = taskn;
		}
		pthread_mutex_unlock(&nlock);

		dprintf("[%s] endtask\n", task->name);
		
		free_task(task);
		task = taskn;
		taskn = NULL;
		pthread_mutex_unlock(&wlock);
		sem_post(&sem);
		goto newtask;
	} else pthread_mutex_unlock(&wlock);

err:
	dprintf("[%s] endtask\n", task->name);
	dprintf("end thread\n");

	thread_sigmask();
	
	ts_free_thread();

	pthread_mutex_lock(&nlock);
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

	free_task(task);
	
	pthread_cond_signal(&ncond);
	pthread_mutex_unlock(&nlock);

	pthread_exit(NULL);
}

ZEND_BEGIN_ARG_INFO(arginfo_create_task, 0)
	ZEND_ARG_INFO(0, taskname)
	ZEND_ARG_INFO(0, filename)
	ZEND_ARG_ARRAY_INFO(0, params, 0)
	ZEND_ARG_INFO(0, logfile)
	ZEND_ARG_INFO(0, logmode)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(create_task) {
	zval *params;
	char *taskname, *filename, *logfile = NULL, *logmode = "ab";
	size_t taskname_len, filename_len, logfile_len = 0, logmode_len = 2;
	task_t *task;
	HashTable *ht;
	zend_long idx;
	zend_string *key;
	zval *val;

	pthread_t thread;
	pthread_attr_t attr;
	int ret;
	struct timespec timeout;
	
	if(mthread != pthread_self()) {
		dprintf("create_task() is not running in the main thread\n");
		return;
	}

	ZEND_PARSE_PARAMETERS_START(3, 5)
		Z_PARAM_STRING(taskname, taskname_len)
		Z_PARAM_STRING(filename, filename_len)
		Z_PARAM_ARRAY(params)
		Z_PARAM_OPTIONAL
		Z_PARAM_STRING(logfile, logfile_len)
		Z_PARAM_STRING(logmode, logmode_len)
	ZEND_PARSE_PARAMETERS_END();

	if(access(filename, F_OK|R_OK) != 0) {
		perror("access() is error");
		RETURN_FALSE;
	}
	
	if(!isRun) RETURN_FALSE;

	ht = Z_ARRVAL_P(params);
	task = (task_t *) malloc(sizeof(task_t));
	memset(task, 0, sizeof(task_t));

	task->name = strndup(taskname, taskname_len);

	dprintf("CREATE_TASK: %s\n", task->name);

	task->argc = zend_hash_num_elements(ht) + 1;
	task->argv = (char**) malloc(sizeof(char*) * task->argc);
	task->argv[0] = strndup(filename, filename_len);

	if(logfile && logfile_len) {
		task->logfile = strndup(logfile, logfile_len);
	}
	if(logmode && logmode_len) {
		task->logmode = strndup(logmode, logmode_len);
	}

	ret = 0;
	ZEND_HASH_FOREACH_KEY_VAL(ht, idx, key, val) {
		convert_to_string(val);
		task->argv[++ret] = strndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
	} ZEND_HASH_FOREACH_END();

	idle:
	pthread_mutex_lock(&wlock);
	if(wthreads) {
		taskn = task;
		pthread_mutex_unlock(&wlock);
		sem_post(&rsem);
		sem_wait(&sem);
		RETURN_TRUE;
	} else {
		pthread_mutex_unlock(&wlock);
		
		pthread_mutex_lock(&nlock);
		if(threads >= maxthreads) {
			pthread_mutex_unlock(&nlock);
			usleep(50);
			if(isRun) goto idle;
		} else pthread_mutex_unlock(&nlock);
	}
	
	if(!isRun) {
		free_task(task);
		RETURN_FALSE;
	}

	pthread_mutex_lock(&nlock);
	if(threads >= maxthreads) {
		pthread_mutex_unlock(&nlock);
		free_task(task);
		RETURN_FALSE;
	}
	threads++;
	pthread_mutex_unlock(&nlock);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create(&thread, &attr, (void*(*)(void*)) thread_task, task);
	if(ret != 0) {
		perror("pthread_create() is error");
	}
	pthread_attr_destroy(&attr);

	if(ret != 0) {
		free_task(task);
	}
	
	sem_wait(&sem);

	RETURN_BOOL(ret == 0);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_wait, 0)
ZEND_ARG_INFO(0, sig)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_wait) {
	task_t *task;
	pthread_t thread;
	zend_long sig;
	int i;
	
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(sig)
	ZEND_PARSE_PARAMETERS_END();
	
	if(sig == SIGUSR1 || sig == SIGUSR2) {
		sig = SIGINT;
		isReload = 1;
	}
	isRun = 0;
	dprintf("TASK_WAIT\n");

	pthread_mutex_lock(&nlock);
	for(i=0; i<threads; i++) sem_post(&rsem);

	task = head_task;
	while(task) {
		thread = task->thread;
		task = task->next;
		dprintf("pthread_kill %d\n", pthread_tid_ex(thread));
		pthread_kill(thread, (int) sig);
	}

	while(threads > 0) {
		pthread_cond_wait(&ncond, &nlock);
	}
	pthread_mutex_unlock(&nlock);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_set_delay, 0)
ZEND_ARG_INFO(0, delay)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_set_delay) {
	zend_long d;
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(d)
	ZEND_PARSE_PARAMETERS_END();
	RETVAL_LONG(delay);
	if(delay > 0) delay = d;
}

ZEND_BEGIN_ARG_INFO(arginfo_task_set_threads, 0)
ZEND_ARG_INFO(0, threads)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_set_threads) {
	zend_long d;
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(d)
	ZEND_PARSE_PARAMETERS_END();
	RETVAL_LONG(maxthreads);
	if(d > 1) maxthreads = d;
}

ZEND_BEGIN_ARG_INFO(arginfo_task_set_debug, 0)
ZEND_ARG_INFO(0, isDebug)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_set_debug) {
	zend_bool d;
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_BOOL(d)
	ZEND_PARSE_PARAMETERS_END();
	RETVAL_BOOL(isDebug);
	isDebug = d;
}

ZEND_BEGIN_ARG_INFO(arginfo_task_set_run, 0)
ZEND_ARG_INFO(0, isRun)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_set_run) {
	zend_bool d = 0;
	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(d)
	ZEND_PARSE_PARAMETERS_END();
	RETVAL_BOOL(isRun);
	isRun = d;
}

const zend_function_entry additional_functions[] = {
	ZEND_FE(create_task, arginfo_create_task)
	ZEND_FE(task_wait, arginfo_task_wait)
	ZEND_FE(task_set_delay, arginfo_task_set_delay)
	ZEND_FE(task_set_threads, arginfo_task_set_threads)
	ZEND_FE(task_set_debug, arginfo_task_set_debug)
	ZEND_FE(task_set_run, arginfo_task_set_run)
	{NULL, NULL, NULL}
};
