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
#include <unistd.h>
#include <fcntl.h>

#include <php.h>
#include <php_main.h>
#include <SAPI.h>
#include <php_output.h>
#include <zend_smart_str.h>
#include <standard/php_var.h>
#include <standard/info.h>
#include <php_network.h>
#include <sockets/php_sockets.h>

#include "func.h"
#include "hash.h"

static sem_t wsem, rsem;
static pthread_mutex_t nlock, wlock;
static pthread_cond_t ncond;
static volatile unsigned int threads = 0;
static volatile unsigned int wthreads = 0;
static volatile zend_bool isRun = 1;
static pthread_t mthread;
static pthread_key_t pkey;

volatile unsigned int maxthreads = 256;
volatile unsigned int delay = 1;
volatile zend_bool isDebug = 0;
volatile zend_bool isReload = 0;

static int php_threadtask_globals_id;
#define SINFO(v) ZEND_TSRMG(php_threadtask_globals_id, php_threadtask_globals_struct *, v)

static long le_threadtask_descriptor;
#define PHP_THREADTASK_DESCRIPTOR "threadtask"

typedef struct _php_threadtask_globals_struct {
	char strftime[20];
} php_threadtask_globals_struct;

typedef struct _task_t {
	pthread_t thread;
	char *name;
	int argc;
	char **argv;
	char *logfile;
	char *logmode;
	FILE *fp;
	sem_t *sem;
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
	sem_init(&wsem, 0, 0);
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
	dprintf("sizeof(value_t) = %lu\n", sizeof(value_t));
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
	sem_destroy(&wsem);
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

	ts_resource(0);

	sem_wait(&wsem);

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
	zend_register_long_constant(ZEND_STRL("THREAD_TASK_NUM"), maxthreads, CONST_CS, PHP_USER_CONSTANT);
	zend_register_long_constant(ZEND_STRL("THREAD_TASK_DELAY"), delay, CONST_CS, PHP_USER_CONSTANT);

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
		if(!task->sem) {
			timeout.tv_sec = delay;
			timeout.tv_nsec = 0;
			sigprocmask(SIG_BLOCK, &waitset, NULL);
			sigtimedwait(&waitset, &info, &timeout);
			if(isRun) goto loop;
		}
	} else {
		php_request_shutdown(NULL);
	}
	
	if(task->sem) {
		sem_post(task->sem);
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
	ZEND_ARG_INFO(1, res)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(create_task) {
	zval *params;
	char *taskname, *filename, *logfile = NULL, *logmode = "ab";
	size_t taskname_len, filename_len, logfile_len = 0, logmode_len = 2;
	task_t *task;
	HashTable *ht;
	zend_long idx;
	zend_string *key;
	zval *val, *res = NULL;

	pthread_t thread;
	pthread_attr_t attr;
	int ret;
	struct timespec timeout;

	ZEND_PARSE_PARAMETERS_START(3, 6)
		Z_PARAM_STRING(taskname, taskname_len)
		Z_PARAM_STRING(filename, filename_len)
		Z_PARAM_ARRAY(params)
		Z_PARAM_OPTIONAL
		Z_PARAM_STRING(logfile, logfile_len)
		Z_PARAM_STRING(logmode, logmode_len)
		Z_PARAM_ZVAL_EX2(res, 0, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if(access(filename, F_OK|R_OK) != 0) {
		perror("access() is error");
		RETURN_FALSE;
	}

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
	
	if(res) {
		task->sem = (sem_t*) malloc(sizeof(sem_t));
		sem_init(task->sem, 0, 0);
		
		ZEND_REGISTER_RESOURCE(res, task->sem, le_threadtask_descriptor);

		dprintf("RESOURCE %p register\n", task->sem);
	}

	ret = 0;
	ZEND_HASH_FOREACH_KEY_VAL(ht, idx, key, val) {
		convert_to_string(val);
		task->argv[++ret] = strndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
	} ZEND_HASH_FOREACH_END();

	dprintf("TASK0: %s\n", taskname);
	idle:
	pthread_mutex_lock(&wlock);
	if(wthreads) {
		if(taskn) {
			pthread_mutex_unlock(&wlock);
			usleep(500);
			goto idle;
		}
		taskn = task;
		pthread_mutex_unlock(&wlock);
		
		sem_post(&rsem);
		
		dprintf("TASK1: %s\n", taskname);
		
		RETURN_TRUE;
	} else {
		pthread_mutex_unlock(&wlock);
		
		pthread_mutex_lock(&nlock);
		if(threads >= maxthreads) {
			pthread_mutex_unlock(&nlock);
			usleep(500);
			goto idle;
		}
		threads++;
		pthread_mutex_unlock(&nlock);
	}
	dprintf("TASK1: %s\n", taskname);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create(&thread, &attr, (void*(*)(void*)) thread_task, task);
	if(ret) {
		errno = ret;
		perror("pthread_create() is error");
		errno = 0;
		free_task(task);
	} else {
		sem_post(&wsem);
	}
	pthread_attr_destroy(&attr);

	RETURN_BOOL(ret == 0);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_is_run, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_is_run) {
	zval *res;
	sem_t *ptr;
	int v = 0;
	
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_RESOURCE(res)
	ZEND_PARSE_PARAMETERS_END();
	
	ptr = (sem_t*) zend_fetch_resource_ex(res, PHP_THREADTASK_DESCRIPTOR, le_threadtask_descriptor);
	if(ptr && !sem_getvalue(ptr, &v) && !v) {
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}
}

ZEND_BEGIN_ARG_INFO(arginfo_task_join, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_join) {
	zval *res;
	sem_t *ptr;
	
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_RESOURCE(res)
	ZEND_PARSE_PARAMETERS_END();
	
	ptr = (sem_t*) zend_fetch_resource_ex(res, PHP_THREADTASK_DESCRIPTOR, le_threadtask_descriptor);
	if(ptr) sem_wait(ptr);
	
	RETURN_BOOL(ptr);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_wait, 0)
ZEND_ARG_INFO(0, sig)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_wait) {
	task_t *task;
	pthread_t thread;
	zend_long sig;
	int i;

	if(mthread != pthread_self()) {
		php_printf("The task_wait function can only be executed in main thread\n");
		return;
	}

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

// ===========================================================================================================
static hash_table_t *share_var_ht = NULL;
static pthread_mutex_t share_var_rlock;
static pthread_mutex_t share_var_wlock;
static volatile int share_var_locks = 0;

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_init, 0, 0, 0)
ZEND_ARG_TYPE_INFO(0, size, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_exists, 0, 0, 1)
ZEND_ARG_VARIADIC_INFO(0, keys)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_get, 0, 0, 0)
ZEND_ARG_VARIADIC_INFO(0, keys)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_get_and_del, 0, 0, 0)
ZEND_ARG_VARIADIC_INFO(0, keys)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_put, 0, 0, 1)
ZEND_ARG_VARIADIC_INFO(0, keys)
ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_inc, 0, 0, 2)
ZEND_ARG_VARIADIC_INFO(0, keys)
ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_set, 0, 0, 2)
ZEND_ARG_VARIADIC_INFO(0, keys)
ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_set_ex, 0, 0, 3)
ZEND_ARG_VARIADIC_INFO(0, keys)
ZEND_ARG_INFO(0, value)
ZEND_ARG_TYPE_INFO(0, expire, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_del, 0, 0, 1)
ZEND_ARG_VARIADIC_INFO(0, keys)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_clean, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_clean_ex, 0, 0, 1)
ZEND_ARG_TYPE_INFO(0, expire, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_count, 0, 0, 0)
ZEND_ARG_VARIADIC_INFO(0, keys)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_destory, 0, 0, 0)
ZEND_END_ARG_INFO()

#define SHARE_VAR_RLOCK() \
	pthread_mutex_lock(&share_var_rlock); \
	if ((++(share_var_locks)) == 1) { \
		pthread_mutex_lock(&share_var_wlock); \
	} \
	pthread_mutex_unlock(&share_var_rlock)

#define SHARE_VAR_RUNLOCK() \
	pthread_mutex_lock(&share_var_rlock); \
	if ((--(share_var_locks)) == 0) { \
		pthread_mutex_unlock(&share_var_wlock); \
	} \
	pthread_mutex_unlock(&share_var_rlock)

#define SHARE_VAR_WLOCK() pthread_mutex_lock(&share_var_wlock)
#define SHARE_VAR_WUNLOCK() pthread_mutex_unlock(&share_var_wlock)

//---------------------------------------------------------------------------------------

#define __NULL (void)0

#define SERIALIZE(z,ok) SERIALIZE_EX(z,__NULL,ok,__NULL,__NULL)
#define SERIALIZE_EX(z,r1,ok,r2,r3) \
	do { \
		php_serialize_data_t var_hash; \
		smart_str buf = {0}; \
		PHP_VAR_SERIALIZE_INIT(var_hash); \
		php_var_serialize(&buf, z, &var_hash); \
		PHP_VAR_SERIALIZE_DESTROY(var_hash); \
		if (EG(exception)) { \
			smart_str_free(&buf); \
			r1; \
		} else if (buf.s) { \
			ok; \
			smart_str_free(&buf); \
			r2; \
		} else { \
			r3; \
		} \
	} while(0)

#define UNSERIALIZE(s,l,ok) UNSERIALIZE_EX(s,l,__NULL,ok,__NULL)
#define UNSERIALIZE_EX(s,l,r,ok,ok2) \
	do { \
		php_unserialize_data_t var_hash; \
		char *__buf = s; \
		const unsigned char *__p = __buf; \
		size_t __buflen = l; \
		PHP_VAR_UNSERIALIZE_INIT(var_hash); \
		zval *retval = var_tmp_var(&var_hash); \
		if(!php_var_unserialize(retval, &__p, __p + __buflen, &var_hash)) { \
			if (!EG(exception)) { \
				php_error_docref(NULL, E_NOTICE, "Error at offset " ZEND_LONG_FMT " of %zd bytes", (zend_long)((char*)__p - __buf), __buflen); \
			} \
			r; \
		} else { \
			ok;ok2; \
		} \
		PHP_VAR_UNSERIALIZE_DESTROY(var_hash); \
	} while(0)

static PHP_FUNCTION(share_var_init)
{
	if(mthread != pthread_self()) {
		php_printf("The share_var_destory function can only be executed in main thread\n");
		return;
	}
	
	if(share_var_ht) return;

	zend_long size = 128;
	if(zend_parse_parameters(ZEND_NUM_ARGS(), "|l", &size) == FAILURE) return;

	pthread_mutex_init(&share_var_rlock, NULL);
	pthread_mutex_init(&share_var_wlock, NULL);
	share_var_ht = (hash_table_t*) malloc(sizeof(hash_table_t));

	RETVAL_BOOL(hash_table_init(share_var_ht, size) == SUCCESS);
}

static PHP_FUNCTION(share_var_exists)
{
	zval *arguments;
	int arg_num = ZEND_NUM_ARGS(), i;
	if(arg_num <= 0) return;
	if(!share_var_ht) return;

	arguments = (zval *) safe_emalloc(sizeof(zval), arg_num, 0);
	if(zend_get_parameters_array_ex(arg_num, arguments) == FAILURE) goto end;

	SHARE_VAR_RLOCK();
	value_t v1 = {.type=HT_T,.ptr=share_var_ht,.expire=0}, v2 = {.type=NULL_T,.expire=0};
	RETVAL_FALSE;
	for(i=0; i<arg_num && v1.type == HT_T; i++) {
		if(i+1 == arg_num) {
			if(Z_TYPE(arguments[i]) == IS_LONG) {
				RETVAL_BOOL(hash_table_index_exists((hash_table_t*) v1.ptr, Z_LVAL(arguments[i])));
			} else {
				convert_to_string(&arguments[i]);
				RETVAL_BOOL(hash_table_exists((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i])));
			}
		} else if(Z_TYPE(arguments[i]) == IS_LONG) {
			if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) break;
		} else {
			convert_to_string(&arguments[i]);
			if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) break;
		}
		v1 = v2;
	}
	SHARE_VAR_RUNLOCK();

	end:
	efree(arguments);
}

static int hash_table_to_zval(bucket_t *p, zval *a) {
	if(p->nKeyLength == 0) {
		switch(p->value.type) {
			case NULL_T:
				add_index_null(a, p->h);
				break;
			case BOOL_T:
				add_index_bool(a, p->h, p->value.b);
				break;
			case CHAR_T:
				add_index_long(a, p->h, p->value.c);
				break;
			case SHORT_T:
				add_index_long(a, p->h, p->value.s);
				break;
			case INT_T:
				add_index_long(a, p->h, p->value.i);
				break;
			case LONG_T:
				add_index_long(a, p->h, p->value.l);
				break;
			case FLOAT_T:
				add_index_double(a, p->h, p->value.f);
				break;
			case DOUBLE_T:
				add_index_double(a, p->h, p->value.d);
				break;
			case STR_T:
				add_index_stringl(a, p->h, p->value.str->str, p->value.str->len);
				break;
			case HT_T: {
				zval z;
				array_init_size(&z, hash_table_num_elements(p->value.ptr));
				hash_table_apply_with_argument(p->value.ptr, (hash_apply_func_arg_t) hash_table_to_zval, &z);
				add_index_zval(a, p->h, &z);
				break;
			}
			case SERI_T: {
				zval rv;
				UNSERIALIZE_EX(p->value.str->str, p->value.str->len, __NULL, ZVAL_COPY(&rv, retval), add_index_zval(a, p->h, &rv));
				break;
			}
			case PTR_T:
				add_index_long(a, p->h, (zend_long) p->value.ptr);
				break;
		}
	} else {
		switch(p->value.type) {
			case NULL_T:
				add_assoc_null_ex(a, p->arKey, p->nKeyLength);
				break;
			case BOOL_T:
				add_assoc_bool_ex(a, p->arKey, p->nKeyLength, p->value.b);
				break;
			case CHAR_T:
				add_assoc_long_ex(a, p->arKey, p->nKeyLength, p->value.c);
				break;
			case SHORT_T:
				add_assoc_long_ex(a, p->arKey, p->nKeyLength, p->value.s);
				break;
			case INT_T:
				add_assoc_long_ex(a, p->arKey, p->nKeyLength, p->value.i);
				break;
			case LONG_T:
				add_assoc_long_ex(a, p->arKey, p->nKeyLength, p->value.l);
				break;
			case FLOAT_T:
				add_assoc_double_ex(a, p->arKey, p->nKeyLength, p->value.f);
				break;
			case DOUBLE_T:
				add_assoc_double_ex(a, p->arKey, p->nKeyLength, p->value.d);
				break;
			case STR_T:
				add_assoc_stringl_ex(a, p->arKey, p->nKeyLength, p->value.str->str, p->value.str->len);
				break;
			case HT_T: {
				zval z;
				array_init_size(&z, hash_table_num_elements(p->value.ptr));
				hash_table_apply_with_argument(p->value.ptr, (hash_apply_func_arg_t) hash_table_to_zval, &z);
				add_assoc_zval_ex(a, p->arKey, p->nKeyLength, &z);
				break;
			}
			case SERI_T: {
				zval rv;
				UNSERIALIZE_EX(p->value.str->str, p->value.str->len, __NULL, ZVAL_COPY(&rv, retval), add_assoc_zval_ex(a, p->arKey, p->nKeyLength, &rv));
				break;
			}
			case PTR_T:
				add_assoc_long_ex(a, p->arKey, p->nKeyLength, (zend_long) p->value.ptr);
				break;
		}
	}
	
	return HASH_TABLE_APPLY_KEEP;
}

void value_to_zval(value_t *v, zval *return_value) {
	switch(v->type) {
		case BOOL_T:
			RETVAL_BOOL(v->b);
			break;
		case CHAR_T:
			RETVAL_LONG(v->c);
			break;
		case SHORT_T:
			RETVAL_LONG(v->s);
			break;
		case INT_T:
			RETVAL_LONG(v->i);
			break;
		case LONG_T:
			RETVAL_LONG(v->l);
			break;
		case FLOAT_T:
			RETVAL_DOUBLE(v->f);
			break;
		case DOUBLE_T:
			RETVAL_DOUBLE(v->d);
			break;
		case STR_T:
			RETVAL_STRINGL(v->str->str, v->str->len);
			break;
		case HT_T:
			array_init_size(return_value, hash_table_num_elements(v->ptr));
			hash_table_apply_with_argument(v->ptr, (hash_apply_func_arg_t) hash_table_to_zval, return_value);
			break;
		case SERI_T: {
			UNSERIALIZE(v->str->str, v->str->len, ZVAL_COPY(return_value, retval));
			break;
		}
		case PTR_T:
			RETVAL_LONG((zend_long) v->ptr);
			break;
		default:
			RETVAL_NULL();
			break;
	}
}

static PHP_FUNCTION(share_var_get)
{
	zval *arguments;
	int arg_num = ZEND_NUM_ARGS(), i;

	if(!share_var_ht) return;

	if(arg_num <= 0) {
		SHARE_VAR_RLOCK();
		array_init_size(return_value, hash_table_num_elements(share_var_ht));
		hash_table_apply_with_argument(share_var_ht, (hash_apply_func_arg_t) hash_table_to_zval, return_value);
		SHARE_VAR_RUNLOCK();
		return;
	}

	arguments = (zval *) safe_emalloc(sizeof(zval), arg_num, 0);
	if(zend_get_parameters_array_ex(arg_num, arguments) == FAILURE) goto end;

	SHARE_VAR_RLOCK();
	value_t v1 = {.type=HT_T,.ptr=share_var_ht,.expire=0}, v2 = {.type=NULL_T,.expire=0};
	for(i=0; i<arg_num && v1.type == HT_T; i++) {
		if(Z_TYPE(arguments[i]) == IS_LONG) {
			if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) break;
		} else {
			convert_to_string(&arguments[i]);
			if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) break;
		}
		if(i == arg_num - 1) value_to_zval(&v2, return_value);
		else v1 = v2;
	}
	SHARE_VAR_RUNLOCK();

	end:
	efree(arguments);
}

static PHP_FUNCTION(share_var_get_and_del)
{
	zval *arguments;
	int arg_num = ZEND_NUM_ARGS(), i;

	if(!share_var_ht) return;

	if(arg_num <= 0) {
		SHARE_VAR_WLOCK();
		array_init_size(return_value, hash_table_num_elements(share_var_ht));
		hash_table_apply_with_argument(share_var_ht, (hash_apply_func_arg_t) hash_table_to_zval, return_value);
		hash_table_clean(share_var_ht);
		SHARE_VAR_WUNLOCK();
		return;
	}

	arguments = (zval *) safe_emalloc(sizeof(zval), arg_num, 0);
	if(zend_get_parameters_array_ex(arg_num, arguments) == FAILURE) goto end;

	SHARE_VAR_WLOCK();
	value_t v1 = {.type=HT_T,.ptr=share_var_ht,.expire=0}, v2 = {.type=NULL_T,.expire=0};
	for(i=0; i<arg_num && v1.type == HT_T; i++) {
		if(Z_TYPE(arguments[i]) == IS_LONG) {
			if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) break;

			if(i == arg_num - 1) {
				value_to_zval(&v2, return_value);
				hash_table_index_del((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]));
			}
		} else {
			convert_to_string(&arguments[i]);
			if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) break;
			
			if(i == arg_num - 1) {
				value_to_zval(&v2, return_value);
				hash_table_del((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]));
			}
		}

		v1 = v2;
	}
	SHARE_VAR_WUNLOCK(); // if(i!=arg_num) {printf("TYPE: %d,%d,%d\n", v1.type, v2.type, i);php_var_dump(&arguments[0], 0);php_var_dump(&arguments[i], 0);}

	end:
	efree(arguments);
}

static int zval_array_to_hash_table(zval *pDest, int num_args, va_list args, zend_hash_key *hash_key);
static void zval_to_value(zval *z, value_t *v) {
	v->expire = 0;
	switch(Z_TYPE_P(z)) {
		case IS_FALSE:
		case IS_TRUE:
			v->type = BOOL_T;
			v->b = Z_TYPE_P(z) == IS_TRUE;
			break;
		case IS_LONG:
			v->type = LONG_T;
			v->l = Z_LVAL_P(z);
			break;
		case IS_DOUBLE:
			v->type = DOUBLE_T;
			v->d = Z_DVAL_P(z);
			break;
		case IS_STRING:
			v->type = STR_T;
			v->str = (string_t*) malloc(sizeof(string_t)+Z_STRLEN_P(z));
			memcpy(v->str->str, Z_STRVAL_P(z), Z_STRLEN_P(z));
			v->str->str[Z_STRLEN_P(z)] = '\0';
			v->str->len = Z_STRLEN_P(z);
			break;
		case IS_ARRAY:
			v->type = HT_T;
			v->ptr = malloc(sizeof(hash_table_t));
			hash_table_init((hash_table_t*) v->ptr, 2);
			zend_hash_apply_with_arguments(Z_ARR_P(z), zval_array_to_hash_table, 1, v->ptr);
			break;
		case IS_OBJECT:
			#define __SERI_OK2 \
				v->type = SERI_T; \
				v->str = (string_t*) malloc(sizeof(string_t)+ZSTR_LEN(buf.s)); \
				memcpy(v->str->str, ZSTR_VAL(buf.s), ZSTR_LEN(buf.s)); \
				v->str->str[ZSTR_LEN(buf.s)] = '\0'; \
				v->str->len = ZSTR_LEN(buf.s)
			SERIALIZE(z, __SERI_OK2);
			#undef __SERI_OK2
			break;
		default:
			v->type = NULL_T;
			break;
	}
}

static int zval_array_to_hash_table(zval *pDest, int num_args, va_list args, zend_hash_key *hash_key) {
	value_t v={.type=NULL_T,.expire=0};
	hash_table_t *ht = va_arg(args, hash_table_t*);

	if(hash_key->key) {
		if(Z_TYPE_P(pDest) == IS_ARRAY) {
			if(hash_table_find(ht, ZSTR_VAL(hash_key->key), ZSTR_LEN(hash_key->key), &v) == FAILURE || v.type != HT_T) {
				zval_to_value(pDest, &v);
				hash_table_update(ht, ZSTR_VAL(hash_key->key), ZSTR_LEN(hash_key->key), &v, NULL);
			} else {
				zend_hash_apply_with_arguments(Z_ARR_P(pDest), zval_array_to_hash_table, 1, v.ptr);
			}
		} else {
			zval_to_value(pDest, &v);
			hash_table_update(ht, ZSTR_VAL(hash_key->key), ZSTR_LEN(hash_key->key), &v, NULL);
		}
	} else {
		if(Z_TYPE_P(pDest) == IS_ARRAY) {
			if(hash_table_index_find(ht, hash_key->h, &v) == FAILURE || v.type != HT_T) {
				zval_to_value(pDest, &v);
				hash_table_index_update(ht, hash_key->h, &v, NULL);
			} else {
				zend_hash_apply_with_arguments(Z_ARR_P(pDest), zval_array_to_hash_table, 1, v.ptr);
			}
		} else {
			zval_to_value(pDest, &v);
			hash_table_index_update(ht, hash_key->h, &v, NULL);
		}
	}

	return ZEND_HASH_APPLY_KEEP;
}

static PHP_FUNCTION(share_var_put)
{
	zval *arguments;
	int arg_num = ZEND_NUM_ARGS(), i;
	if(arg_num <= 0) return;

	if(!share_var_ht) return;

	arguments = (zval *) safe_emalloc(sizeof(zval), arg_num, 0);
	if(zend_get_parameters_array_ex(arg_num, arguments) == FAILURE) goto end;

	SHARE_VAR_WLOCK();
	if(arg_num == 1) {
		if(Z_TYPE(arguments[0]) == IS_ARRAY) {
			zend_hash_apply_with_arguments(Z_ARR(arguments[0]), zval_array_to_hash_table, 1, share_var_ht);
			RETVAL_TRUE;
		} else {
			value_t v3;
			zval_to_value(&arguments[0], &v3);
			RETVAL_BOOL(hash_table_next_index_insert(share_var_ht, &v3, NULL) == SUCCESS);
		}
	} else {
		value_t v1 = {.type=HT_T,.ptr=share_var_ht,.expire=0}, v2;
		RETVAL_FALSE;
		for(i=0; i<arg_num; i++) {
			v2.type = NULL_T;
			if(i+2 == arg_num) {
				if(Z_TYPE(arguments[i+1]) == IS_ARRAY) {
					if(Z_TYPE(arguments[i]) == IS_LONG) {
						if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE || v2.type != HT_T) {
							zval_to_value(&arguments[i+1], &v2);
							RETVAL_BOOL(hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL) == SUCCESS);
						} else {
							zend_hash_apply_with_arguments(Z_ARR(arguments[i+1]), zval_array_to_hash_table, 1, v2.ptr);
							RETVAL_TRUE;
						}
					} else {
						convert_to_string(&arguments[i]);
						if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE || v2.type != HT_T) {
							zval_to_value(&arguments[i+1], &v2);
							RETVAL_BOOL(hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL) == SUCCESS);
						} else {
							zend_hash_apply_with_arguments(Z_ARR(arguments[i+1]), zval_array_to_hash_table, 1, v2.ptr);
							RETVAL_TRUE;
						}
					}
				} else {
					zval_to_value(&arguments[i+1], &v2);
					if(Z_TYPE(arguments[i]) == IS_LONG) {
						RETVAL_BOOL(hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL) == SUCCESS);
					} else {
						convert_to_string(&arguments[i]);
						RETVAL_BOOL(hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL) == SUCCESS);
					}
				}
				break;
			} else if(Z_TYPE(arguments[i]) == IS_LONG) {
				if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) {
					v2.type = HT_T;
					v2.ptr = malloc(sizeof(hash_table_t));
					hash_table_init(v2.ptr, 2);
					hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
				} else {
					if(v2.type != HT_T) {
						v2.type = HT_T;
						v2.ptr = malloc(sizeof(hash_table_t));
						hash_table_init(v2.ptr, 2);
						hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
					}
				}
			} else {
				convert_to_string(&arguments[i]);
				if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) {
					v2.type = HT_T;
					v2.ptr = malloc(sizeof(hash_table_t));
					hash_table_init(v2.ptr, 2);
					hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
				} else {
					if(v2.type != HT_T) {
						v2.type = HT_T;
						v2.ptr = malloc(sizeof(hash_table_t));
						hash_table_init(v2.ptr, 2);
						hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
					}
				}
			}
			v1 = v2;
		}
	}
	SHARE_VAR_WUNLOCK();

	end:
	efree(arguments);
}

#define VALUE_ADD(k,v,t) \
	switch(dst->type) { \
		case BOOL_T: \
			dst->v = dst->b + src->k;\
			dst->type = t;\
			break; \
		case CHAR_T: \
			dst->v = dst->c + src->k;\
			dst->type = t; \
			break; \
		case SHORT_T: \
			dst->v = dst->s + src->k;\
			dst->type = t; \
			break; \
		case INT_T: \
			dst->v = dst->i + src->k;\
			dst->type = t; \
			break; \
		case LONG_T: \
			dst->l = dst->l + src->k;\
			break; \
		case FLOAT_T: \
			dst->v = dst->f + src->k;\
			dst->type = t; \
			break; \
		case DOUBLE_T: \
			dst->d = dst->d + src->k;\
			break; \
		default: \
			break; \
	}

static void value_add(value_t *dst, value_t *src) {
	if(dst->type == HT_T) {
		hash_table_next_index_insert(dst->ptr, src, NULL);
	} else {
		switch(src->type) {
			case BOOL_T:
				VALUE_ADD(b,i,INT_T);
				break;
			case CHAR_T:
				VALUE_ADD(c,i,INT_T);
				break;
			case SHORT_T:
				VALUE_ADD(s,i,INT_T);
				break;
			case INT_T:
				VALUE_ADD(i,i,INT_T);
				break;
			case LONG_T:
				VALUE_ADD(l,l,LONG_T);
				break;
			case FLOAT_T:
				VALUE_ADD(f,f,FLOAT_T);
				break;
			case DOUBLE_T:
				VALUE_ADD(d,d,DOUBLE_T);
				break;
			case STR_T:
				if(dst->type == STR_T) {
					string_t *s = (string_t*) malloc(sizeof(string_t)+dst->str->len+src->str->len);
					s->len = dst->str->len+src->str->len;
					memcpy(s->str, dst->str->str, dst->str->len);
					memcpy(s->str + dst->str->len, src->str->str, src->str->len);
					s->str[s->len] = '\0';
					//free(dst->str);
					dst->str = s;
					hash_table_value_free(src);
				} else {
					dst->type = STR_T;
					dst->str = src->str;
				}
				break;
			default:
				hash_table_value_free(src);
				break;
		}
	}
}

static PHP_FUNCTION(share_var_inc)
{
	zval *arguments;
	int arg_num = ZEND_NUM_ARGS(), i;
	if(arg_num <= 1) return;

	if(!share_var_ht) return;

	arguments = (zval *) safe_emalloc(sizeof(zval), arg_num, 0);
	if(zend_get_parameters_array_ex(arg_num, arguments) == FAILURE) goto end;

	RETVAL_FALSE;

	SHARE_VAR_WLOCK();
	{
		value_t v1 = {.type=HT_T,.ptr=share_var_ht,.expire=0}, v2, v3 = {.type=NULL_T,.expire=0};
		ulong h;
		for(i=0; i<arg_num; i++) {
			v2.type = NULL_T;
			if(i+2 == arg_num) {
				zval_to_value(&arguments[i+1], &v2);
				if(Z_TYPE(arguments[i]) == IS_LONG) {
					if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v3) == FAILURE) {
						if(hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL) == SUCCESS) {
							value_to_zval(&v2, return_value);
						}
					} else {
						value_add(&v3, &v2);
						if(v3.type != HT_T) {
							if(hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v3, NULL) == SUCCESS) {
								value_to_zval(&v3, return_value);
							}
						} else RETVAL_LONG(hash_table_num_elements(v3.ptr));
					}
				} else {
					h = zend_get_hash_value(Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]));
					if(hash_table_quick_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), h, &v3) == FAILURE) {
						if(hash_table_quick_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), h, &v2, NULL) == SUCCESS) {
							value_to_zval(&v2, return_value);
						}
					} else {
						value_add(&v3, &v2);
						if(v3.type != HT_T) {
							if(hash_table_quick_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), h, &v3, NULL) == SUCCESS) {
								value_to_zval(&v3, return_value);
							}
						} else RETVAL_LONG(hash_table_num_elements(v3.ptr));
					}
				}
				break;
			} else if(Z_TYPE(arguments[i]) == IS_LONG) {
				if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) {
					v2.type = HT_T;
					v2.ptr = malloc(sizeof(hash_table_t));
					hash_table_init(v2.ptr, 2);
					hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
				} else {
					if(v2.type != HT_T) {
						v2.type = HT_T;
						v2.ptr = malloc(sizeof(hash_table_t));
						hash_table_init(v2.ptr, 2);
						hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
					}
				}
			} else {
				convert_to_string(&arguments[i]);
				if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) {
					v2.type = HT_T;
					v2.ptr = malloc(sizeof(hash_table_t));
					hash_table_init(v2.ptr, 2);
					hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
				} else {
					if(v2.type != HT_T) {
						v2.type = HT_T;
						v2.ptr = malloc(sizeof(hash_table_t));
						hash_table_init(v2.ptr, 2);
						hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
					}
				}
			}
			v1 = v2;
		}
	}
	SHARE_VAR_WUNLOCK();

	end:
	efree(arguments);
}

static PHP_FUNCTION(share_var_set)
{
	zval *arguments;
	int arg_num = ZEND_NUM_ARGS(), i;
	if(arg_num <= 1) return;

	arguments = (zval *) safe_emalloc(sizeof(zval), arg_num, 0);
	if(zend_get_parameters_array_ex(arg_num, arguments) == FAILURE) goto end;

	SHARE_VAR_WLOCK();
	value_t v1 = {.type=HT_T,.ptr=share_var_ht,.expire=0}, v2 = {.type=NULL_T,.expire=0};
	RETVAL_FALSE;
	for(i=0; i<arg_num && v1.type == HT_T; i++) {
		if(i+2 == arg_num) {
			zval_to_value(&arguments[i+1], &v2);
			if(Z_TYPE(arguments[i]) == IS_LONG) {
				RETVAL_BOOL(hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL) == SUCCESS);
			} else {
				convert_to_string(&arguments[i]);
				RETVAL_BOOL(hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL) == SUCCESS);
			}
			break;
		} else if(Z_TYPE(arguments[i]) == IS_LONG) {
			if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) {
				v2.type = HT_T;
				v2.ptr = malloc(sizeof(hash_table_t));
				hash_table_init(v2.ptr, 2);
				hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
			} else {
				if(v2.type != HT_T) {
					v2.type = HT_T;
					v2.ptr = malloc(sizeof(hash_table_t));
					hash_table_init(v2.ptr, 2);
					hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
				}
			}
		} else {
			convert_to_string(&arguments[i]);
			if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) {
				v2.type = HT_T;
				v2.ptr = malloc(sizeof(hash_table_t));
				hash_table_init(v2.ptr, 2);
				hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
			} else {
				if(v2.type != HT_T) {
					v2.type = HT_T;
					v2.ptr = malloc(sizeof(hash_table_t));
					hash_table_init(v2.ptr, 2);
					hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
				}
			}
		}
		v1 = v2;
	}
	SHARE_VAR_WUNLOCK();

	end:
	efree(arguments);
}

static PHP_FUNCTION(share_var_set_ex)
{
	zval *arguments;
	int arg_num = ZEND_NUM_ARGS(), i;
	if(arg_num <= 2) return;

	arguments = (zval *) safe_emalloc(sizeof(zval), arg_num, 0);
	if(zend_get_parameters_array_ex(arg_num, arguments) == FAILURE) goto end;

	SHARE_VAR_WLOCK();
	value_t v1 = {.type=HT_T,.ptr=share_var_ht,.expire=0}, v2 = {.type=NULL_T,.expire=0};
	RETVAL_FALSE;
	for(i=0; i<arg_num && v1.type == HT_T; i++) {
		if(i+3 == arg_num) {
			zval_to_value(&arguments[i+1], &v2);
			v2.expire = (int) Z_LVAL(arguments[i+2]);
			if(Z_TYPE(arguments[i]) == IS_LONG) {
				RETVAL_BOOL(hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL) == SUCCESS);
			} else {
				convert_to_string(&arguments[i]);
				RETVAL_BOOL(hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL) == SUCCESS);
			}
			break;
		} else if(Z_TYPE(arguments[i]) == IS_LONG) {
			if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) {
				v2.type = HT_T;
				v2.ptr = malloc(sizeof(hash_table_t));
				hash_table_init(v2.ptr, 2);
				hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
			} else {
				if(v2.type != HT_T) {
					v2.type = HT_T;
					v2.ptr = malloc(sizeof(hash_table_t));
					hash_table_init(v2.ptr, 2);
					hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
				}
			}
		} else {
			convert_to_string(&arguments[i]);
			if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) {
				v2.type = HT_T;
				v2.ptr = malloc(sizeof(hash_table_t));
				hash_table_init(v2.ptr, 2);
				hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
			} else {
				if(v2.type != HT_T) {
					v2.type = HT_T;
					v2.ptr = malloc(sizeof(hash_table_t));
					hash_table_init(v2.ptr, 2);
					hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
				}
			}
		}
		v1 = v2;
	}
	SHARE_VAR_WUNLOCK();

	end:
	efree(arguments);
}

static PHP_FUNCTION(share_var_del)
{
	zval *arguments;
	int arg_num = ZEND_NUM_ARGS(), i;
	if(arg_num <= 0) return;
	if(!share_var_ht) return;

	arguments = (zval *) safe_emalloc(sizeof(zval), arg_num, 0);
	if(zend_get_parameters_array_ex(arg_num, arguments) == FAILURE) goto end;

	SHARE_VAR_WLOCK();
	value_t v1 = {.type=HT_T,.ptr=share_var_ht,.expire=0}, v2 = {.type=NULL_T,.expire=0};
	RETVAL_FALSE;
	for(i=0; i<arg_num && v1.type == HT_T; i++) {
		if(i+1 == arg_num) {
			if(Z_TYPE(arguments[i]) == IS_LONG) {
				RETVAL_BOOL(hash_table_index_del((hash_table_t*) v1.ptr, Z_LVAL(arguments[i])) == SUCCESS);
			} else {
				convert_to_string(&arguments[i]);
				RETVAL_BOOL(hash_table_del((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i])) == SUCCESS);
			}
		} else if(Z_TYPE(arguments[i]) == IS_LONG) {
			if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) break;
		} else {
			convert_to_string(&arguments[i]);
			if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) break;
		}
		v1 = v2;
	}
	SHARE_VAR_WUNLOCK();

	end:
	efree(arguments);
}

static PHP_FUNCTION(share_var_clean)
{
	if(!share_var_ht) return;

	int n;
	SHARE_VAR_WLOCK();
	n = hash_table_num_elements(share_var_ht);
	hash_table_clean(share_var_ht);
	SHARE_VAR_WUNLOCK();

	RETVAL_LONG(n);
}

static int hash_table_clean_ex(bucket_t *p, int *ex) {
	if(p->value.expire && p->value.expire < *ex) {
		return HASH_TABLE_APPLY_REMOVE;
	} else if(p->value.type == HT_T) {
		hash_table_apply_with_argument(p->value.ptr, (hash_apply_func_arg_t) hash_table_clean_ex, ex);
	}
	
	return HASH_TABLE_APPLY_KEEP;
}

static PHP_FUNCTION(share_var_clean_ex)
{
	zend_long d;
	int n;
	
	if(!share_var_ht) return;
	
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(d)
	ZEND_PARSE_PARAMETERS_END();
	
	if(d <= 0) return;
	
	n = (int) d;

	SHARE_VAR_WLOCK();
	hash_table_apply_with_argument(share_var_ht, (hash_apply_func_arg_t) hash_table_clean_ex, &n);
	n = hash_table_num_elements(share_var_ht);
	SHARE_VAR_WUNLOCK();

	RETVAL_LONG(n);
}

static PHP_FUNCTION(share_var_count)
{
	zval *arguments;
	int arg_num = ZEND_NUM_ARGS(), i;

	if(!share_var_ht) return;

	if(arg_num <= 0) {
		SHARE_VAR_RLOCK();
		RETVAL_LONG(hash_table_num_elements(share_var_ht));
		SHARE_VAR_RUNLOCK();
		return;
	}

	arguments = (zval *) safe_emalloc(sizeof(zval), arg_num, 0);
	if(zend_get_parameters_array_ex(arg_num, arguments) == FAILURE) goto end;

	SHARE_VAR_RLOCK();
	value_t v1 = {.type=HT_T,.ptr=share_var_ht,.expire=0}, v2 = {.type=NULL_T,.expire=0};
	for(i=0; i<arg_num && v1.type == HT_T; i++) {
		if(Z_TYPE(arguments[i]) == IS_LONG) {
			if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) break;
		} else {
			convert_to_string(&arguments[i]);
			if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) break;
		}
		if(i == arg_num - 1) {
			switch(v2.type) {
				case STR_T:
					RETVAL_LONG(- (zend_long) v2.str->len);
					break;
				case SERI_T:
					RETVAL_TRUE;
					break;
				case HT_T:
					RETVAL_LONG(hash_table_num_elements(v2.ptr));
					break;
				default:
					RETVAL_FALSE;
					break;
			}
		} else v1 = v2;
	}
	SHARE_VAR_RUNLOCK();

	end:
	efree(arguments);
}

static PHP_FUNCTION(share_var_destory)
{
	if(mthread != pthread_self()) {
		php_printf("The share_var_destory function can only be executed in main thread\n");
		return;
	}

	if(!share_var_ht) return;

	int n = hash_table_num_elements(share_var_ht);
	pthread_mutex_destroy(&share_var_rlock);
	pthread_mutex_destroy(&share_var_wlock);
	hash_table_destroy(share_var_ht);
	
	free(share_var_ht);
	share_var_ht = NULL;

	RETVAL_LONG(n);
}

// ===========================================================================================================

#if PHP_VERSION_ID < 80000
php_socket *socket_import_file_descriptor(PHP_SOCKET socket) {
#ifdef SO_DOMAIN
	int						type;
	socklen_t				type_len = sizeof(type);
#endif
	php_socket 				*retsock;
	php_sockaddr_storage	addr;
	socklen_t				addr_len = sizeof(addr);
#ifndef PHP_WIN32
	int					 t;
#endif

    retsock = php_create_socket();
    retsock->bsd_socket = socket;

    /* determine family */
#ifdef SO_DOMAIN
    if (getsockopt(socket, SOL_SOCKET, SO_DOMAIN, &type, &type_len) == 0) {
		retsock->type = type;
	} else
#endif
	if (getsockname(socket, (struct sockaddr*)&addr, &addr_len) == 0) {
		retsock->type = addr.ss_family;
	} else {
		goto error;
	}

    /* determine blocking mode */
#ifndef PHP_WIN32
    t = fcntl(socket, F_GETFL);
    if (t == -1) {
		goto error;
    } else {
    	retsock->blocking = !(t & O_NONBLOCK);
    }
#endif

    return retsock;

error:
	efree(retsock);
	return NULL;
}
#endif

#if PHP_VERSION_ID < 80000
ZEND_BEGIN_ARG_INFO_EX(arginfo_socket_export_fd, 0, 0, 1)
ZEND_ARG_TYPE_INFO(0, socket, IS_RESOURCE, 0)
ZEND_ARG_TYPE_INFO(0, is_close, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(socket_export_fd) {
	zval *zv;
	php_socket *sock;
	zend_bool is_close = 0;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(is_close)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((sock = (php_socket *) zend_fetch_resource(Z_RES_P(zv), php_sockets_le_socket_name, php_sockets_le_socket())) == NULL) {
		RETURN_FALSE;
	}
	
	RETVAL_LONG(sock->bsd_socket);
	
	if(is_close) {
		sock->bsd_socket = -1;
	}
}
#else
ZEND_BEGIN_ARG_INFO_EX(arginfo_socket_export_fd, 0, 0, 1)
ZEND_ARG_OBJ_INFO(0, socket, Socket, 0)
ZEND_ARG_TYPE_INFO(0, is_close, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(socket_export_fd) {
	zval *zsocket;
	php_socket *socket;
	zend_bool is_close = 0;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "O|b", &zsocket, socket_ce, &is_close) == FAILURE) {
		RETURN_THROWS();
	}

	socket = Z_SOCKET_P(zsocket);
	ENSURE_SOCKET_VALID(socket);
	
	RETVAL_LONG(socket->bsd_socket);
	
	if(is_close) {
		socket->bsd_socket = -1;
	}
}
#endif

ZEND_BEGIN_ARG_INFO_EX(arginfo_socket_import_fd, 0, 0, 1)
ZEND_ARG_TYPE_INFO(0, sockfd, IS_LONG, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(socket_import_fd) {
	zend_long fd = -1;
	php_socket *sock;
	
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(fd)
	ZEND_PARSE_PARAMETERS_END();
	
	if(fd <= 0) RETURN_FALSE;

#if PHP_VERSION_ID >= 80000	
	object_init_ex(return_value, socket_ce);
	sock = Z_SOCKET_P(return_value);
	if (!socket_import_file_descriptor(fd, sock)) {
		zval_ptr_dtor(return_value);
		RETURN_FALSE;
	}
#else
	sock = socket_import_file_descriptor(fd);
	if(sock) {
		RETURN_RES(zend_register_resource(sock, php_sockets_le_socket()));
	} else RETURN_FALSE;
#endif
}

// ===========================================================================================================

static const zend_function_entry ext_functions[] = {
	ZEND_FE(create_task, arginfo_create_task)
	ZEND_FE(task_join, arginfo_task_join)
	ZEND_FE(task_is_run, arginfo_task_is_run)
	ZEND_FE(task_wait, arginfo_task_wait)
	ZEND_FE(task_set_delay, arginfo_task_set_delay)
	ZEND_FE(task_set_threads, arginfo_task_set_threads)
	ZEND_FE(task_set_debug, arginfo_task_set_debug)
	ZEND_FE(task_set_run, arginfo_task_set_run)
	
	PHP_FE(share_var_init, arginfo_share_var_init)
	PHP_FE(share_var_exists, arginfo_share_var_exists)
	PHP_FE(share_var_get, arginfo_share_var_get)
	PHP_FE(share_var_get_and_del, arginfo_share_var_get_and_del)
	PHP_FE(share_var_put, arginfo_share_var_put)
	PHP_FE(share_var_inc, arginfo_share_var_inc)
	PHP_FE(share_var_set, arginfo_share_var_set)
	PHP_FE(share_var_set_ex, arginfo_share_var_set_ex)
	PHP_FE(share_var_del, arginfo_share_var_del)
	PHP_FE(share_var_clean, arginfo_share_var_clean)
	PHP_FE(share_var_clean_ex, arginfo_share_var_clean_ex)
	PHP_FE(share_var_count, arginfo_share_var_count)
	PHP_FE(share_var_destory, arginfo_share_var_destory)
	
	PHP_FE(socket_export_fd, arginfo_socket_export_fd)
	PHP_FE(socket_import_fd, arginfo_socket_import_fd)
	
	{NULL, NULL, NULL}
};

// -----------------------------------------------------------------------------------------------------------

static void php_threadtask_globals_ctor(php_threadtask_globals_struct *php_threadtask_globals) {
}

static void php_threadtask_globals_dtor(php_threadtask_globals_struct *php_threadtask_globals) {
}

static void php_destroy_threadtask(zend_resource *rsrc) {
	sem_t *ptr = (sem_t *) rsrc->ptr;
	
	sem_destroy(ptr);
	
	free(ptr);
	
	dprintf("RESOURCE %p destroy\n", ptr);
}

static PHP_MINIT_FUNCTION(threadtask) {
	ts_allocate_id(&php_threadtask_globals_id, sizeof(php_threadtask_globals_struct), (ts_allocate_ctor) php_threadtask_globals_ctor, (ts_allocate_dtor) php_threadtask_globals_dtor);
	le_threadtask_descriptor = zend_register_list_destructors_ex(php_destroy_threadtask, NULL, PHP_THREADTASK_DESCRIPTOR, module_number);
	return SUCCESS;
}

static PHP_MSHUTDOWN_FUNCTION(threadtask) {
	return SUCCESS;
}

static PHP_MINFO_FUNCTION(threadtask) {
	php_info_print_table_start();
	php_info_print_table_row(2, "threadtask", "active");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

zend_module_entry threadtask_module_entry = {
	STANDARD_MODULE_HEADER,
	"threadtask",
	ext_functions,
	PHP_MINIT(threadtask),
	PHP_MSHUTDOWN(threadtask),
	NULL,
	NULL,
	PHP_MINFO(threadtask),
	PHP_VERSION,
	STANDARD_MODULE_PROPERTIES
};

