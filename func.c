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
#include <zend_smart_str.h>
#include <standard/php_var.h>

#include "func.h"
#include "hash.h"

static sem_t sem, rsem;
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

// ===========================================================================================================
static hash_table_t *share_var_ht = NULL;
static pthread_mutex_t share_var_rlock;
static pthread_mutex_t share_var_wlock;
static int share_var_locks = 0;

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_init, 0, 0, 1)
ZEND_ARG_INFO(0, size)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_exists, 0, 0, 1)
ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_get, 0, 0, 0)
ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_put, 0, 0, 1)
ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_del, 0, 0, 1)
ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_clean, 0, 0, 0)
ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_share_var_destory, 0, 0, 0)
ZEND_ARG_INFO(0, key)
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

#define UNSERIALIZE(s,l,ok) UNSERIALIZE_EX(s,l,__NULL,ok)
#define UNSERIALIZE_EX(s,l,r,ok) \
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
			ok; \
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
	value_t v1 = {.type=HT_T,.ptr=share_var_ht}, v2 = {.type=NULL_T};
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
			convert_to_string(&arguments[0]);
			if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) break;
		}
		v1 = v2;
	}
	SHARE_VAR_RUNLOCK();

	end:
	efree(arguments);
}

static void hash_table_to_zval(bucket_t *p, zval *a) {
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
				UNSERIALIZE(p->value.str->str, p->value.str->len, add_index_zval(a, p->h, retval));
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
				UNSERIALIZE(p->value.str->str, p->value.str->len, add_assoc_zval_ex(a, p->arKey, p->nKeyLength, retval));
				break;
			}
			case PTR_T:
				add_assoc_long_ex(a, p->arKey, p->nKeyLength, (zend_long) p->value.ptr);
				break;
		}
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
	value_t v1 = {.type=HT_T,.ptr=share_var_ht}, v2 = {.type=NULL_T};
	for(i=0; i<arg_num && v1.type == HT_T; i++) {
		if(Z_TYPE(arguments[i]) == IS_LONG) {
			if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE) break;
		} else {
			convert_to_string(&arguments[0]);
			if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE) break;
		}
		v1 = v2;
	}
	if(i == arg_num) {
		switch(v2.type) {
			case NULL_T:
				RETVAL_NULL();
				break;
			case BOOL_T:
				RETVAL_BOOL(v2.b);
				break;
			case CHAR_T:
				RETVAL_LONG(v2.c);
				break;
			case SHORT_T:
				RETVAL_LONG(v2.s);
				break;
			case INT_T:
				RETVAL_LONG(v2.i);
				break;
			case LONG_T:
				RETVAL_LONG(v2.l);
				break;
			case FLOAT_T:
				RETVAL_DOUBLE(v2.f);
				break;
			case DOUBLE_T:
				RETVAL_DOUBLE(v2.d);
				break;
			case STR_T:
				RETVAL_STRINGL(v2.str->str, v2.str->len);
				break;
			case HT_T:
				array_init_size(return_value, hash_table_num_elements(v2.ptr));
				hash_table_apply_with_argument(v2.ptr, (hash_apply_func_arg_t) hash_table_to_zval, return_value);
				break;
			case SERI_T: {
				UNSERIALIZE(v2.str->str, v2.str->len, ZVAL_COPY(return_value, retval));
				break;
			}
			case PTR_T:
				RETVAL_LONG((zend_long) v2.ptr);
				break;
		}
	}
	SHARE_VAR_RUNLOCK();

	end:
	efree(arguments);
}

static int zval_array_to_hash_table(zval *pDest, int num_args, va_list args, zend_hash_key *hash_key);
static void zval_to_value(zval *z, value_t *v) {
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
	value_t v={.type=NULL_T};
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
		} else {
			value_t v3;
			zval_to_value(&arguments[0], &v3);
			hash_table_next_index_insert(share_var_ht, &v3, NULL);
		}
	} else {
		value_t v1 = {.type=HT_T,.ptr=share_var_ht}, v2;
		for(i=0; i<arg_num; i++) {
			v2.type = NULL_T;
			if(i+2 == arg_num) {
				if(Z_TYPE(arguments[i+1]) == IS_ARRAY) {
					if(Z_TYPE(arguments[i]) == IS_LONG) {
						if(hash_table_index_find((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2) == FAILURE || v2.type != HT_T) {
							zval_to_value(&arguments[i+1], &v2);
							hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
						} else {
							zend_hash_apply_with_arguments(Z_ARR(arguments[i+1]), zval_array_to_hash_table, 1, v2.ptr);
						}
					} else {
						convert_to_string(&arguments[i]);
						if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE || v2.type != HT_T) {
							zval_to_value(&arguments[i+1], &v2);
							hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
						} else {
							zend_hash_apply_with_arguments(Z_ARR(arguments[i+1]), zval_array_to_hash_table, 1, v2.ptr);
						}
					}
				} else {
					zval_to_value(&arguments[i+1], &v2);
					if(Z_TYPE(arguments[i]) == IS_LONG) {
						hash_table_index_update((hash_table_t*) v1.ptr, Z_LVAL(arguments[i]), &v2, NULL);
					} else {
						convert_to_string(&arguments[i]);
						hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL);
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
				convert_to_string(&arguments[0]);
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

	RETVAL_TRUE;
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
	value_t v1 = {.type=HT_T,.ptr=share_var_ht}, v2 = {.type=NULL_T};
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
			convert_to_string(&arguments[0]);
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
	share_var_ht = NULL;

	RETVAL_LONG(n);
}

// ===========================================================================================================

const zend_function_entry additional_functions[] = {
	ZEND_FE(create_task, arginfo_create_task)
	ZEND_FE(task_wait, arginfo_task_wait)
	ZEND_FE(task_set_delay, arginfo_task_set_delay)
	ZEND_FE(task_set_threads, arginfo_task_set_threads)
	ZEND_FE(task_set_debug, arginfo_task_set_debug)
	ZEND_FE(task_set_run, arginfo_task_set_run)
	
	PHP_FE(share_var_init, arginfo_share_var_init)
	PHP_FE(share_var_exists, arginfo_share_var_exists)
	PHP_FE(share_var_get, arginfo_share_var_get)
	PHP_FE(share_var_put, arginfo_share_var_put)
	PHP_FE(share_var_del, arginfo_share_var_del)
	PHP_FE(share_var_clean, arginfo_share_var_clean)
	PHP_FE(share_var_destory, arginfo_share_var_destory)
	
	{NULL, NULL, NULL}
};
