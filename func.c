#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __USE_GNU
#include <pthread.h>
#undef __USE_GNU
#include <unistd.h>
#include <semaphore.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/prctl.h>
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
#include <zend_exceptions.h>
#include <spl/spl_functions.h>

#include "func.h"
#include "hash.h"

static sem_t rsem;
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

static ts_rsrc_id php_threadtask_globals_id;
#define SINFO(v) ZEND_TSRMG(php_threadtask_globals_id, php_threadtask_globals_struct *, v)

static long le_threadtask_descriptor;
#define PHP_THREADTASK_DESCRIPTOR "threadtask"

static long le_ts_var_descriptor;
#define PHP_TS_VAR_DESCRIPTOR "ts_var_t"

static ts_hash_table_t ts_var;

typedef struct _timeout_t {
	pthread_t tid;
	double sec;
	struct _timeout_t *prev;
	struct _timeout_t *next;
} timeout_t;

static timeout_t *thead = NULL;
static pthread_mutex_t tlock;

typedef struct _php_threadtask_globals_struct {
	char strftime[20];
	zend_bool is_throw_exit;
	int timestamp;
	timeout_t *timeout;
} php_threadtask_globals_struct;

typedef struct _wait_t {
	pthread_t tid;
	sem_t sem;
	zend_bool isExit;
} wait_t;

typedef struct _task_t {
	pthread_t thread;
	char *name;
	int argc;
	char **argv;
	char *logfile;
	char *logmode;
	FILE *fp;
	wait_t *wait;
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

	if(SINFO(timestamp) == t) return SINFO(strftime);

	SINFO(timestamp) = t;
	tmp = localtime(&t);
	if (tmp == NULL) {
		perror("localtime error");
		return "0000-00-00 00:00:00";
	}

	if (strftime(SINFO(strftime), sizeof(SINFO(strftime)), "%F %T", tmp) == 0) {
		perror("strftime error");
		return "";
	}

	return SINFO(strftime);
}

#define MICRO_IN_SEC 1000000.00

double microtime()
{
	struct timeval tp = {0};

	if (gettimeofday(&tp, NULL)) {
		return 0;
	}   

	return (double)(tp.tv_sec + tp.tv_usec / MICRO_IN_SEC);
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

	for(sig=1; sig<=NSIG; sig++) {
		sigaddset(&set, sig);
	}

	pthread_sigmask(SIG_SETMASK, &set, NULL);
}

void thread_init() {
	sem_init(&rsem, 0, 0);
	pthread_mutex_init(&nlock, NULL);
	pthread_mutex_init(&wlock, NULL);
	pthread_cond_init(&ncond, NULL);
	pthread_key_create(&pkey, NULL);
	pthread_setspecific(pkey, NULL);
#ifdef LOCK_TIMEOUT
	pthread_key_create(&tskey, ts_table_table_tid_destroy);
	pthread_setspecific(tskey, NULL);
#endif
	pthread_mutex_init(&tlock, NULL);

	ts_hash_table_init(&ts_var, 2);
	thread_sigmask();

	mthread = pthread_self();
}

#ifdef MYSQLI_USE_MYSQLND
zend_class_entry *mysqli_link_class_entry;
zend_class_entry *mysqli_stmt_class_entry;
#endif

void thread_running() {
	if(PG(error_log) == NULL) PG(display_errors) = 0;

#ifdef MYSQLI_USE_MYSQLND
	{
		zend_string *mysqli_str = zend_string_init(ZEND_STRL("mysqli"), 0);
		zend_string *stmt_str = zend_string_init(ZEND_STRL("mysqli_stmt"), 0);

		mysqli_link_class_entry = zend_lookup_class(mysqli_str);
		dprintf("mysqli_link_class_entry: %p\n", mysqli_link_class_entry);

		mysqli_stmt_class_entry = zend_lookup_class(stmt_str);
		dprintf("mysqli_stmt_class_entry: %p\n", mysqli_stmt_class_entry);

		zend_string_release_ex(mysqli_str, 0);
		zend_string_release_ex(stmt_str, 0);
	}
#endif

	dprintf("sizeof(Bucket) = %lu\n", sizeof(Bucket));
	dprintf("sizeof(HashTable) = %lu\n", sizeof(HashTable));
	dprintf("sizeof(zval) = %lu\n", sizeof(zval));

	dprintf("sizeof(type_t) = %lu\n", sizeof(type_t));
	dprintf("sizeof(value_t) = %lu\n", sizeof(value_t));
	dprintf("sizeof(bucket_t) = %lu\n", sizeof(bucket_t));
	dprintf("sizeof(hash_table_t) = %lu\n", sizeof(hash_table_t));
	dprintf("sizeof(ts_hash_table_t) = %lu\n", sizeof(ts_hash_table_t));
#ifdef LOCK_TIMEOUT
	dprintf("sizeof(tskey_hash_table_t) = %lu\n", sizeof(tskey_hash_table_t));
#endif
	dprintf("sizeof(sem_t) = %lu\n", sizeof(sem_t));
	dprintf("sizeof(pthread_t) = %lu\n", sizeof(pthread_t));
	dprintf("sizeof(pthread_mutex_t) = %lu\n", sizeof(pthread_mutex_t));
	dprintf("sizeof(pthread_rwlock_t) = %lu\n", sizeof(pthread_rwlock_t));
}

void thread_destroy() {
	if(taskn) {
		free_task(taskn);
		taskn = NULL;
	}
	
	ts_hash_table_destroy_ex(&ts_var, 0);

	pthread_mutex_destroy(&tlock);

#ifdef LOCK_TIMEOUT
	pthread_key_delete(tskey);
#endif
	pthread_key_delete(pkey);
	pthread_cond_destroy(&ncond);
	pthread_mutex_destroy(&nlock);
	pthread_mutex_destroy(&wlock);
	sem_destroy(&rsem);
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
	if(task->wait) {
		task->wait->tid = task->thread;
	}

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

#ifdef LOCK_TIMEOUT
	pthread_setspecific(tskey, NULL);
#endif

	dprintf("begin thread\n");

newtask:
	dprintf("[%s] newtask\n", task->name);

	prctl(PR_SET_NAME, (unsigned long) task->name);

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
	if(PG(error_log) == NULL) PG(display_errors) = 0;

	zend_register_string_constant(ZEND_STRL("THREAD_TASK_NAME"), task->name, CONST_CS, PHP_USER_CONSTANT);

	cli_register_file_handles();

	dprintf("[%s] running\n", task->name);

#if PHP_VERSION_ID >= 70400
	CG(skip_shebang) = 1;
#endif

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
		if(!task->wait) {
			timeout.tv_sec = delay;
			timeout.tv_nsec = 0;
			sigprocmask(SIG_BLOCK, &waitset, NULL);
			sigtimedwait(&waitset, &info, &timeout);
			if(isRun) goto loop;
		}
	} else {
		php_request_shutdown(NULL);
	}

	SG(request_info).argc = 0;
	SG(request_info).argv = NULL;
	
	if(task->wait) {
		sem_post(&task->wait->sem);
		if(task->wait->isExit) {
			goto err;
		} else {
			task->wait->isExit = 1;
		}
	}
	
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
	zval *val, *res = NULL;

	pthread_t thread;
	pthread_attr_t attr;
	int ret;

	ZEND_PARSE_PARAMETERS_START(3, 6)
		Z_PARAM_STRING(taskname, taskname_len)
		Z_PARAM_STRING(filename, filename_len)
		Z_PARAM_ARRAY(params)
		Z_PARAM_OPTIONAL
		Z_PARAM_STRING(logfile, logfile_len)
		Z_PARAM_STRING(logmode, logmode_len)
		Z_PARAM_ZVAL_DEREF(res)
	ZEND_PARSE_PARAMETERS_END();

	if(access(filename, F_OK|R_OK) != 0) {
		fprintf(stderr, "Could not open input file: %s\n", filename);
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
		task->wait = (wait_t*) malloc(sizeof(wait_t));
		memset(task->wait, 0, sizeof(wait_t));
		sem_init(&task->wait->sem, 0, 0);
		
		zval_ptr_dtor(res);
		ZVAL_RES(res, zend_register_resource(task->wait, le_threadtask_descriptor));
		
		dprintf("RESOURCE %p register\n", task->wait);
	}

	ret = 0;
	ZEND_HASH_FOREACH_VAL(ht, val) {
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
	} else if(task->wait) {
		task->wait->tid = thread;
	}
	pthread_attr_destroy(&attr);

	RETURN_BOOL(ret == 0);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_is_run, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_is_run) {
	zval *res;
	wait_t *ptr;
	int v = 0;
	
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_RESOURCE(res)
	ZEND_PARSE_PARAMETERS_END();
	
	ptr = (wait_t*) zend_fetch_resource_ex(res, PHP_THREADTASK_DESCRIPTOR, le_threadtask_descriptor);
	if(ptr && !sem_getvalue(&ptr->sem, &v) && !v) {
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
	wait_t *ptr;
	
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_RESOURCE(res)
	ZEND_PARSE_PARAMETERS_END();
	
	ptr = (wait_t*) zend_fetch_resource_ex(res, PHP_THREADTASK_DESCRIPTOR, le_threadtask_descriptor);
	if(ptr) sem_wait(&ptr->sem);
	
	RETURN_BOOL(ptr);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_kill, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_TYPE_INFO(0, sig, IS_LONG, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_kill) {
	zval *res;
	zend_long sig = SIGINT;
	wait_t *ptr;
	int ret;
	
	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_RESOURCE(res)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(sig)
	ZEND_PARSE_PARAMETERS_END();
	
	ptr = (wait_t*) zend_fetch_resource_ex(res, PHP_THREADTASK_DESCRIPTOR, le_threadtask_descriptor);
	if(ptr) {
		if(!ptr->isExit) {
			ptr->isExit = 1;

			dprintf("pthread_kill %d\n", pthread_tid_ex(ptr->tid));
			ret = pthread_kill(ptr->tid, (int) sig);
			if(ret) {
				errno = ret;
				perror("pthread_create() is error");
				errno = 0;
				ptr->isExit = 0;
			}
		}

		RETURN_BOOL(ptr->isExit);
	} else {
		RETURN_FALSE;
	}
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
	dprintf("TASK_WAIT begin\n");

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
	
	dprintf("TASK_WAIT end\n");
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

ZEND_BEGIN_ARG_INFO(arginfo_task_get_num, 0)
ZEND_ARG_INFO(0, is_max)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_get_num) {
	zend_bool is_max = 0;
	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(is_max)
	ZEND_PARSE_PARAMETERS_END();

	if(is_max) {
		RETVAL_LONG(maxthreads);
	} else {
		pthread_mutex_lock(&nlock);
		RETVAL_LONG(threads);
		pthread_mutex_unlock(&nlock);
	}
}

ZEND_BEGIN_ARG_INFO(arginfo_task_get_run, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_get_run) {
	ZEND_PARSE_PARAMETERS_NONE();

	RETVAL_BOOL(isRun);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_get_debug, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_get_debug) {
	ZEND_PARSE_PARAMETERS_NONE();

	RETVAL_BOOL(isDebug);
}

ZEND_BEGIN_ARG_INFO(arginfo_task_get_delay, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(task_get_delay) {
	ZEND_PARSE_PARAMETERS_NONE();

	RETVAL_LONG(delay);
}

// -----------------------------------------------------------------------------------------------------------

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_pthread_sigmask, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, mode, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, signals, IS_ARRAY, 0)
	ZEND_ARG_INFO(1, old_signals)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(pthread_sigmask) {
	zend_long how, signo;
	zval *user_set, *user_oldset = NULL, *user_signo;
	sigset_t set, oldset;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "la|z/", &how, &user_set, &user_oldset) == FAILURE) {
		RETURN_FALSE;
	}

	if (sigemptyset(&set) != 0 || sigemptyset(&oldset) != 0) { 
		php_error_docref(NULL, E_WARNING, "%s", strerror(errno));
		RETURN_FALSE;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(user_set), user_signo) {
		signo = zval_get_long(user_signo);
		if (sigaddset(&set, signo) != 0) { 
			php_error_docref(NULL, E_WARNING, "%s", strerror(errno));
			RETURN_FALSE;
		}
	} ZEND_HASH_FOREACH_END();

	if (pthread_sigmask(how, &set, &oldset) != 0) { 
		php_error_docref(NULL, E_WARNING, "%s", strerror(errno));
		RETURN_FALSE;
	}

	if (user_oldset != NULL) {
		if (Z_TYPE_P(user_oldset) != IS_ARRAY) {
			zval_ptr_dtor(user_oldset);
			array_init(user_oldset);
		} else {
			zend_hash_clean(Z_ARRVAL_P(user_oldset));
		}

		for (signo = 1; signo < NSIG; ++signo) {
			if (sigismember(&oldset, signo) != 1) { 
				continue;
			}
			add_next_index_long(user_oldset, signo);
		}
	}

	RETURN_TRUE;
}

ZEND_BEGIN_ARG_INFO(arginfo_pthread_yield, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(pthread_yield) {
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(pthread_yield() == 0);
}

// ===========================================================================================================

static zend_class_entry *spl_ce_GoExitException;

static int go_exit_handler(zend_execute_data *execute_data) {
	if(SINFO(is_throw_exit)) {
		const zend_op *opline = EX(opline);
		zval ex;
		zend_object *obj;
		zval _exit_status;
		zval *exit_status = NULL;

		if (opline->op1_type != IS_UNUSED) {
			if (opline->op1_type == IS_CONST) {
				// see: https://github.com/php/php-src/commit/e70618aff6f447a298605d07648f2ce9e5a284f5
#ifdef EX_CONSTANT
				exit_status = EX_CONSTANT(opline->op1);
#else
				exit_status = RT_CONSTANT(opline, opline->op1);
#endif
			} else {
				exit_status = EX_VAR(opline->op1.var);
			}
			if (Z_ISREF_P(exit_status)) {
				exit_status = Z_REFVAL_P(exit_status);
			}
			ZVAL_DUP(&_exit_status, exit_status);
			exit_status = &_exit_status;
		} else {
			exit_status = &_exit_status;
			ZVAL_NULL(exit_status);
		}
		obj = zend_throw_exception(spl_ce_GoExitException, "In go function is run exit()", 0);
		ZVAL_OBJ(&ex, obj);
		Z_TRY_ADDREF_P(exit_status);
		zend_update_property(spl_ce_GoExitException, Z_OBJ_PROP(&ex), ZEND_STRL("status"), exit_status);
	}

	return ZEND_USER_OPCODE_DISPATCH;
}

ZEND_BEGIN_ARG_INFO(arginfo_spl_ce_GoExitException_getStatus, 0)
ZEND_END_ARG_INFO()

static PHP_METHOD(spl_ce_GoExitException, getStatus) {
	zval *prop, rv;
	zend_string *status;

	ZEND_PARSE_PARAMETERS_NONE();

	status = zend_string_init(ZEND_STRL("status"), 0);
	prop = zend_read_property_ex(Z_OBJCE_P(ZEND_THIS), Z_OBJ_PROP(ZEND_THIS), status, 0, &rv);
	zend_string_release_ex(status, 0);

	RETURN_ZVAL(prop, 1, 0);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_go, 0, 0, 1)
ZEND_ARG_INFO(0, function_name)
ZEND_ARG_VARIADIC_INFO(0, parameters)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(go) {
	zend_fcall_info fci; 
	zend_fcall_info_cache fci_cache;

	ZEND_PARSE_PARAMETERS_START(1, -1)
		Z_PARAM_FUNC(fci, fci_cache)
	#if PHP_VERSION_ID >= 80000
		Z_PARAM_VARIADIC_WITH_NAMED(fci.params, fci.param_count, fci.named_params)
	#else
		Z_PARAM_VARIADIC('*', fci.params, fci.param_count)
	#endif
	ZEND_PARSE_PARAMETERS_END();

	fci.retval = return_value;
	
	zend_bool b = SINFO(is_throw_exit);
	SINFO(is_throw_exit) = 1;

	zend_try {
		if(zend_call_function(&fci, &fci_cache) == FAILURE) {
			RETVAL_FALSE;
		}
	} zend_catch {
		EG(exit_status) = 0;
	} zend_end_try();

	SINFO(is_throw_exit) = b;
}

ZEND_BEGIN_ARG_INFO(arginfo_call_and_free_shutdown, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(call_and_free_shutdown) {
	ZEND_PARSE_PARAMETERS_NONE();

	zend_try {
		php_call_shutdown_functions();
	} zend_catch {
		EG(exit_status) = 0;
	} zend_end_try();

	php_free_shutdown_functions();
}

// ===========================================================================================================

ZEND_BEGIN_ARG_INFO(arginfo_set_timeout, 0)
ZEND_ARG_INFO(0, seconds)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(set_timeout) {
	zend_long sec = 1;
	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(sec)
	ZEND_PARSE_PARAMETERS_END();

	if(SINFO(timeout) || sec <= 0) {
		RETURN_FALSE;
	}
	
	timeout_t *t = (timeout_t*) malloc(sizeof(timeout_t));
	t->tid = pthread_self();
	t->sec = microtime() + sec;
	SINFO(timeout) = t;

	pthread_mutex_lock(&tlock);
	if(thead) {
		t->next = thead->next;
		thead->next = t;
		t->next->prev = t;
		t->prev = thead;
	} else {
		t->prev = t;
		t->next = t;
		thead = t;
	}
	pthread_mutex_unlock(&tlock);

	RETURN_TRUE;
}

ZEND_BEGIN_ARG_INFO(arginfo_clear_timeout, 0)
ZEND_ARG_INFO(0, seconds)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(clear_timeout) {
	ZEND_PARSE_PARAMETERS_NONE();

	if(SINFO(timeout) == NULL) {
		RETURN_FALSE;
	} else {
		timeout_t *t = SINFO(timeout);

		pthread_mutex_lock(&tlock);
		if(thead == t) {
			if(t->next == t) {
				thead = NULL;
			} else {
				thead = t->next;
				thead->prev = t->prev;
				thead->prev->next = thead;
			}
		} else {
			t->prev->next = t->next;
			t->next->prev = t->prev;
		}
		pthread_mutex_unlock(&tlock);
		
		free(t);

		SINFO(timeout) = NULL;

		RETURN_TRUE;
	}
}

ZEND_BEGIN_ARG_INFO(arginfo_trigger_timeout, 0)
ZEND_ARG_INFO(0, sig)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(trigger_timeout) {
	zend_long sig = SIGALRM;
	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(sig)
	ZEND_PARSE_PARAMETERS_END();

	timeout_t *t;
	double sec = microtime();

	pthread_mutex_lock(&tlock);
	t = thead;
	while(t) {
		if(t->sec <= sec) {
			pthread_kill(t->tid, sig);
		}
		if(t->next == thead) break;
		t = t->next;
	}
	pthread_mutex_unlock(&tlock);

	RETURN_TRUE;
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
		const unsigned char *__p = (const unsigned char *) __buf; \
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
			case LONG_T:
				add_index_long(a, p->h, p->value.l);
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
			case TS_HT_T: {
				zval z;
				array_init_size(&z, hash_table_num_elements(p->value.ptr));
				ts_hash_table_rd_lock(p->value.ptr);
				hash_table_apply_with_argument(p->value.ptr, (hash_apply_func_arg_t) hash_table_to_zval, &z);
				ts_hash_table_rd_unlock(p->value.ptr);
				add_index_zval(a, p->h, &z);
				break;
			}
		}
	} else {
		switch(p->value.type) {
			case NULL_T:
				add_assoc_null_ex(a, p->arKey, p->nKeyLength);
				break;
			case BOOL_T:
				add_assoc_bool_ex(a, p->arKey, p->nKeyLength, p->value.b);
				break;
			case LONG_T:
				add_assoc_long_ex(a, p->arKey, p->nKeyLength, p->value.l);
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
			case TS_HT_T: {
				zval z;
				array_init_size(&z, hash_table_num_elements(p->value.ptr));
				ts_hash_table_rd_lock(p->value.ptr);
				hash_table_apply_with_argument(p->value.ptr, (hash_apply_func_arg_t) hash_table_to_zval, &z);
				ts_hash_table_rd_unlock(p->value.ptr);
				add_assoc_zval_ex(a, p->arKey, p->nKeyLength, &z);
				break;
			}
		}
	}
	
	return HASH_TABLE_APPLY_KEEP;
}

void value_to_zval(value_t *v, zval *return_value) {
	switch(v->type) {
		case BOOL_T:
			RETVAL_BOOL(v->b);
			break;
		case LONG_T:
			RETVAL_LONG(v->l);
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
		case TS_HT_T:
			array_init_size(return_value, hash_table_num_elements(v->ptr));
			ts_hash_table_rd_lock(v->ptr);
			hash_table_apply_with_argument(v->ptr, (hash_apply_func_arg_t) hash_table_to_zval, return_value);
			ts_hash_table_rd_unlock(v->ptr);
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

#if PHP_VERSION_ID < 70300
#define GC_RECURSIVE_BEGIN(z) do { \
	if(!ZEND_HASH_APPLY_PROTECTION(z) || (z)->u.v.nApplyCount++ <= 1) {
#define GC_RECURSIVE_END(z) \
			if(ZEND_HASH_APPLY_PROTECTION(z)) (z)->u.v.nApplyCount--; \
		} else { \
			const char *space; \
			const char *class_name = get_active_class_name(&space); \
			zend_error(E_WARNING, "%s%s%s() does not handle circular references", class_name, space, get_active_function_name()); \
		} \
	} while(0)
#else
#define GC_RECURSIVE_BEGIN(z) \
	do { \
		if(!(GC_FLAGS(z) & GC_IMMUTABLE) && GC_IS_RECURSIVE(z)) { \
			const char *space; \
			const char *class_name = get_active_class_name(&space); \
			zend_error(E_WARNING, "%s%s%s() does not handle circular references", class_name, space, get_active_function_name()); \
		} else { \
			GC_TRY_PROTECT_RECURSION(z)
#define GC_RECURSIVE_END(z) \
			GC_TRY_UNPROTECT_RECURSION(z); \
		} \
	} while(0)
#endif

static int zval_array_to_hash_table(zval *pDest, int num_args, va_list args, zend_hash_key *hash_key);
static void zval_to_value(zval *z, value_t *v) {
	v->expire = 0;
	while(Z_ISREF_P(z)) z = Z_REFVAL_P(z);
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
			GC_RECURSIVE_BEGIN(Z_ARR_P(z));
			v->type = HT_T;
			v->ptr = malloc(sizeof(hash_table_t));
			hash_table_init((hash_table_t*) v->ptr, 2);
			zend_hash_apply_with_arguments(Z_ARR_P(z), zval_array_to_hash_table, 1, v->ptr);
			GC_RECURSIVE_END(Z_ARR_P(z));
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
	
	while(Z_ISREF_P(pDest)) pDest = Z_REFVAL_P(pDest);

	if(hash_key->key) {
		if(Z_TYPE_P(pDest) == IS_ARRAY) {
			if(hash_table_find(ht, ZSTR_VAL(hash_key->key), ZSTR_LEN(hash_key->key), &v) == FAILURE || v.type != HT_T) {
				zval_to_value(pDest, &v);
				hash_table_update(ht, ZSTR_VAL(hash_key->key), ZSTR_LEN(hash_key->key), &v, NULL);
			} else {
				GC_RECURSIVE_BEGIN(Z_ARR_P(pDest));
				zend_hash_apply_with_arguments(Z_ARR_P(pDest), zval_array_to_hash_table, 1, v.ptr);
				GC_RECURSIVE_END(Z_ARR_P(pDest));
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
				GC_RECURSIVE_BEGIN(Z_ARR_P(pDest));
				zend_hash_apply_with_arguments(Z_ARR_P(pDest), zval_array_to_hash_table, 1, v.ptr);
				GC_RECURSIVE_END(Z_ARR_P(pDest));
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
			GC_RECURSIVE_BEGIN(Z_ARR(arguments[0]));
			zend_hash_apply_with_arguments(Z_ARR(arguments[0]), zval_array_to_hash_table, 1, share_var_ht);
			GC_RECURSIVE_END(Z_ARR(arguments[0]));
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
							GC_RECURSIVE_BEGIN(Z_ARR(arguments[i+1]));
							zend_hash_apply_with_arguments(Z_ARR(arguments[i+1]), zval_array_to_hash_table, 1, v2.ptr);
							GC_RECURSIVE_END(Z_ARR(arguments[i+1]));
							RETVAL_TRUE;
						}
					} else {
						convert_to_string(&arguments[i]);
						if(hash_table_find((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2) == FAILURE || v2.type != HT_T) {
							zval_to_value(&arguments[i+1], &v2);
							RETVAL_BOOL(hash_table_update((hash_table_t*) v1.ptr, Z_STRVAL(arguments[i]), Z_STRLEN(arguments[i]), &v2, NULL) == SUCCESS);
						} else {
							GC_RECURSIVE_BEGIN(Z_ARR(arguments[i+1]));
							zend_hash_apply_with_arguments(Z_ARR(arguments[i+1]), zval_array_to_hash_table, 1, v2.ptr);
							GC_RECURSIVE_END(Z_ARR(arguments[i+1]));
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
		case LONG_T: \
			dst->l = dst->l + src->k;\
			break; \
		case DOUBLE_T: \
			dst->d = dst->d + src->k;\
			break; \
		default: \
			dst->v = src->k; \
			dst->type = t; \
			break; \
	}

static void value_add(value_t *dst, value_t *src) {
	if(dst->type == HT_T) {
		hash_table_next_index_insert(dst->ptr, src, NULL);
	} else {
		switch(src->type) {
			case NULL_T:
				src->b = 0;
			case BOOL_T:
				VALUE_ADD(b,l,LONG_T);
				break;
			case LONG_T:
				VALUE_ADD(l,l,LONG_T);
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
	} else if(p->value.type == TS_HT_T) {
		ts_hash_table_t *ptr = (ts_hash_table_t*) p->value.ptr;
		if(ptr->expire && ptr->expire < *ex) return HASH_TABLE_APPLY_REMOVE;
		ts_hash_table_wr_lock(ptr);
		hash_table_apply_with_argument(p->value.ptr, (hash_apply_func_arg_t) hash_table_clean_ex, ex);
		ts_hash_table_wr_unlock(ptr);
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

// -----------------------------------------------------------------------------------------------------------

static int hash_table_to_zval_wr(bucket_t *p, zval *a) {
	if(p->nKeyLength == 0) {
		switch(p->value.type) {
			case NULL_T:
				add_index_null(a, p->h);
				break;
			case BOOL_T:
				add_index_bool(a, p->h, p->value.b);
				break;
			case LONG_T:
				add_index_long(a, p->h, p->value.l);
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
			case TS_HT_T: {
				zval z;
				array_init_size(&z, hash_table_num_elements(p->value.ptr));
				ts_hash_table_wr_lock(p->value.ptr);
				hash_table_apply_with_argument(p->value.ptr, (hash_apply_func_arg_t) hash_table_to_zval_wr, &z);
				ts_hash_table_wr_unlock(p->value.ptr);
				add_index_zval(a, p->h, &z);
				break;
			}
		}
	} else {
		switch(p->value.type) {
			case NULL_T:
				add_assoc_null_ex(a, p->arKey, p->nKeyLength);
				break;
			case BOOL_T:
				add_assoc_bool_ex(a, p->arKey, p->nKeyLength, p->value.b);
				break;
			case LONG_T:
				add_assoc_long_ex(a, p->arKey, p->nKeyLength, p->value.l);
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
			case TS_HT_T: {
				zval z;
				array_init_size(&z, hash_table_num_elements(p->value.ptr));
				ts_hash_table_wr_lock(p->value.ptr);
				hash_table_apply_with_argument(p->value.ptr, (hash_apply_func_arg_t) hash_table_to_zval_wr, &z);
				ts_hash_table_wr_unlock(p->value.ptr);
				add_assoc_zval_ex(a, p->arKey, p->nKeyLength, &z);
				break;
			}
		}
	}
	
	return HASH_TABLE_APPLY_KEEP;
}

void value_to_zval_wr(value_t *v, zval *return_value) {
	switch(v->type) {
		case BOOL_T:
			RETVAL_BOOL(v->b);
			break;
		case LONG_T:
			RETVAL_LONG(v->l);
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
		case TS_HT_T:
			array_init_size(return_value, hash_table_num_elements(v->ptr));
			ts_hash_table_wr_lock(v->ptr);
			hash_table_apply_with_argument(v->ptr, (hash_apply_func_arg_t) hash_table_to_zval_wr, return_value);
			ts_hash_table_wr_unlock(v->ptr);
			break;
		default:
			RETVAL_NULL();
			break;
	}
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_declare, 1)
ZEND_ARG_INFO(0, key)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 1)
ZEND_ARG_TYPE_INFO(0, is_fd, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_declare) {
	zend_string *key = NULL;
	zend_long index = 0;
	zend_bool is_null = 0;
	zval *zv = NULL;
	zend_bool is_fd = 0;

	ts_hash_table_t *ts_ht;
	value_t v = {.expire=0};

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_STR_OR_LONG_OR_NULL(key, index, is_null);
		Z_PARAM_OPTIONAL
		Z_PARAM_RESOURCE_OR_NULL(zv)
		Z_PARAM_BOOL(is_fd)
	ZEND_PARSE_PARAMETERS_END();
	
	if(zv) {
		if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
			RETURN_FALSE;
		}
	} else {
		ts_ht = &ts_var;
	}

	if(is_null) {
		ts_hash_table_rd_lock(ts_ht);
		ts_hash_table_ref(ts_ht);
		ts_hash_table_rd_unlock(ts_ht);
	} else {
		if(key) {
			zend_long h = zend_get_hash_value(ZSTR_VAL(key), ZSTR_LEN(key));
			
			ts_hash_table_rd_lock(ts_ht);
			if(hash_table_quick_find(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v) == SUCCESS && v.type == TS_HT_T) {
				ts_hash_table_ref(v.ptr);
				ts_hash_table_rd_unlock(ts_ht);
			} else {
				ts_hash_table_rd_unlock(ts_ht);
				ts_hash_table_wr_lock(ts_ht);
				if(hash_table_quick_find(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v) == FAILURE || v.type != TS_HT_T) {
					v.type = TS_HT_T;
					v.ptr = (ts_hash_table_t *) malloc(sizeof(ts_hash_table_t));
					ts_hash_table_init(v.ptr, 2);
					hash_table_quick_update(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v, NULL);
				}
				ts_hash_table_ref(v.ptr);
				ts_hash_table_wr_unlock(ts_ht);
			}
		} else {
			ts_hash_table_rd_lock(ts_ht);
			if(hash_table_index_find(&ts_ht->ht, index, &v) == SUCCESS && v.type == TS_HT_T) {
				ts_hash_table_ref(v.ptr);
				ts_hash_table_rd_unlock(ts_ht);
			} else {
				ts_hash_table_rd_unlock(ts_ht);
				ts_hash_table_wr_lock(ts_ht);
				if(hash_table_index_find(&ts_ht->ht, index, &v) == FAILURE || v.type != TS_HT_T) {
					v.type = TS_HT_T;
					v.ptr = (ts_hash_table_t *) malloc(sizeof(ts_hash_table_t));
					ts_hash_table_init(v.ptr, 2);
					hash_table_index_update(&ts_ht->ht, index, &v, NULL);
				}
				ts_hash_table_ref(v.ptr);
				ts_hash_table_wr_unlock(ts_ht);
			}
		}
		
		ts_ht = (ts_hash_table_t*) v.ptr;
	}

	if(is_fd) {
		ts_hash_table_lock(ts_ht);
		if(!ts_ht->fds[0] && !ts_ht->fds[1] && socketpair(AF_UNIX, SOCK_STREAM, 0, ts_ht->fds) != 0) {
			ts_ht->fds[0] = 0;
			ts_ht->fds[1] = 0;
		}
		ts_hash_table_unlock(ts_ht);
	}

	RETURN_RES(zend_register_resource(ts_ht, le_ts_var_descriptor));
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_fd, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_TYPE_INFO(0, is_write, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_fd) {
	zval *zv;
	zend_bool is_write = 0;
	
	ts_hash_table_t *ts_ht;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(is_write)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}

	if(is_write) {
		socket_import_fd(ts_ht->fds[1], return_value);
	} else {
		socket_import_fd(ts_ht->fds[0], return_value);
	}
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_expire, 2)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_TYPE_INFO(0, expire, IS_LONG, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_expire) {
	zval *zv;
	zend_long expire = 0;
	
	ts_hash_table_t *ts_ht;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_LONG(expire)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}

	ts_hash_table_wr_lock(ts_ht);
	ts_ht->expire = expire;
	ts_hash_table_wr_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_exists, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_exists) {
	zval *zv;
	zend_string *key = NULL;
	zend_long index = 0;
	
	ts_hash_table_t *ts_ht;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_STR_OR_LONG(key, index);
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}

	ts_hash_table_rd_lock(ts_ht);
	if(key) {
		RETVAL_BOOL(hash_table_exists(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key)));
	} else {
		RETVAL_BOOL(hash_table_index_exists(&ts_ht->ht, index));
	}
	ts_hash_table_rd_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_set, 3)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_INFO(0, key)
ZEND_ARG_INFO(0, val)
ZEND_ARG_TYPE_INFO(0, expire, IS_LONG, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_set) {
	zval *zv;
	zend_string *key = NULL;
	zend_long index = 0;
	zend_bool is_null = 0;
	zval *val;
	zend_long expire = 0;
	
	ts_hash_table_t *ts_ht;
	value_t v;

	ZEND_PARSE_PARAMETERS_START(3, 4)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_STR_OR_LONG_OR_NULL(key, index, is_null)
		Z_PARAM_ZVAL(val)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(expire);
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}
	
	zval_to_value(val, &v);
	v.expire = expire;

	ts_hash_table_wr_lock(ts_ht);
	if(is_null) {
		RETVAL_BOOL(hash_table_next_index_insert(&ts_ht->ht, &v, NULL) == SUCCESS);
	} else if(key) {
		RETVAL_BOOL(hash_table_update(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), &v, NULL) == SUCCESS);
	} else {
		RETVAL_BOOL(hash_table_index_update(&ts_ht->ht, index, &v, NULL) == SUCCESS);
	}
	ts_hash_table_wr_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_push, 2)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_INFO(0, val)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_push) {
	zval *zv;
	zval *args;
	int i, argc, n = 0;
	
	ts_hash_table_t *ts_ht;
	value_t v;

	ZEND_PARSE_PARAMETERS_START(2, -1)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_VARIADIC('+', args, argc)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}
	
	ts_hash_table_wr_lock(ts_ht);
	for(i=0; i<argc; i++) {
		zval_to_value(&args[i], &v);
		if(hash_table_next_index_insert(&ts_ht->ht, &v, NULL) == SUCCESS) n++;
		else hash_table_value_free(&v);
	}
	ts_hash_table_wr_unlock(ts_ht);

	RETURN_LONG(n);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_pop, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_INFO(1, key)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_pop) {
	zval *zv;
	zval *key = NULL;
	
	ts_hash_table_t *ts_ht;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL_DEREF(key)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}
	
	ts_hash_table_wr_lock(ts_ht);
	if(ts_ht->ht.pListTail) {
		value_to_zval_wr(&ts_ht->ht.pListTail->value, return_value);
		if(key) {
			zval_ptr_dtor(key);
			if(ts_ht->ht.pListTail->nKeyLength == 0) {
				ZVAL_LONG(key, ts_ht->ht.pListTail->h);
			} else {
				ZVAL_STRINGL(key, ts_ht->ht.pListTail->arKey, ts_ht->ht.pListTail->nKeyLength);
			}
		}
		hash_table_bucket_delete(&ts_ht->ht, ts_ht->ht.pListTail);
	}
	ts_hash_table_wr_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_shift, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_INFO(1, key)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_shift) {
	zval *zv;
	zval *key = NULL;
	
	ts_hash_table_t *ts_ht;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL_DEREF(key)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}
	
	ts_hash_table_wr_lock(ts_ht);
	if(ts_ht->ht.pListHead) {
		value_to_zval_wr(&ts_ht->ht.pListHead->value, return_value);
		if(key) {
			zval_ptr_dtor(key);
			if(ts_ht->ht.pListHead->nKeyLength == 0) {
				ZVAL_LONG(key, ts_ht->ht.pListHead->h);
			} else {
				ZVAL_STRINGL(key, ts_ht->ht.pListHead->arKey, ts_ht->ht.pListHead->nKeyLength);
			}
		}
		hash_table_bucket_delete(&ts_ht->ht, ts_ht->ht.pListHead);
	}
	ts_hash_table_wr_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_minmax, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_TYPE_INFO(0, is_max, _IS_BOOL, 0)
ZEND_ARG_TYPE_INFO(0, is_key, _IS_BOOL, 0)
ZEND_ARG_INFO(1, key)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_minmax) {
	zval *zv;
	zval *key = NULL;
	zend_bool is_max = 0, is_key = 0;
	
	ts_hash_table_t *ts_ht;
	bucket_t *p = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 4)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(is_max)
		Z_PARAM_BOOL(is_key)
		Z_PARAM_ZVAL_DEREF(key)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}
	
	ts_hash_table_rd_lock(ts_ht);
	if(hash_table_minmax(&ts_ht->ht, is_key ? compare_key : compare_value, is_max, &p) == SUCCESS) {
		value_to_zval(&p->value, return_value);
		if(key) {
			zval_ptr_dtor(key);
			if(p->nKeyLength == 0) {
				ZVAL_LONG(key, p->h);
			} else {
				ZVAL_STRINGL(key, p->arKey, p->nKeyLength);
			}
		}
	} else {
		RETVAL_FALSE;
	}
	ts_hash_table_rd_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_get, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_INFO(0, key)
ZEND_ARG_TYPE_INFO(0, is_del, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_get) {
	zval *zv;
	zend_string *key = NULL;
	zend_long index = 0;
	zend_bool is_null = 1;
	zend_bool is_del = 0;
	
	ts_hash_table_t *ts_ht;
	value_t v;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR_OR_LONG_OR_NULL(key, index, is_null)
		Z_PARAM_BOOL(is_del)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}

	if(is_null) {
		ts_hash_table_rd_lock(ts_ht);
		array_init_size(return_value, hash_table_num_elements(&ts_ht->ht));
		hash_table_apply_with_argument(&ts_ht->ht, (hash_apply_func_arg_t) hash_table_to_zval, return_value);
		ts_hash_table_rd_unlock(ts_ht);
	} else if(is_del) {
		ts_hash_table_wr_lock(ts_ht);
		if(key) {
			zend_long h = zend_get_hash_value(ZSTR_VAL(key), ZSTR_LEN(key));
			if(hash_table_quick_find(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v) == SUCCESS) {
				value_to_zval_wr(&v, return_value);
				hash_table_quick_del(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h);
			}
		} else {
			if(hash_table_index_find(&ts_ht->ht, index, &v) == SUCCESS) {
				value_to_zval_wr(&v, return_value);
				hash_table_index_del(&ts_ht->ht, index);
			}
		}
		ts_hash_table_wr_unlock(ts_ht);
	} else {
		ts_hash_table_rd_lock(ts_ht);
		if(key) {
			if(hash_table_find(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), &v) == SUCCESS) {
				value_to_zval(&v, return_value);
			}
		} else {
			if(hash_table_index_find(&ts_ht->ht, index, &v) == SUCCESS) {
				value_to_zval(&v, return_value);
			}
		}
		ts_hash_table_rd_unlock(ts_ht);
	}
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_get_or_set, 3)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_INFO(0, key)
ZEND_ARG_INFO(0, callback)
ZEND_ARG_TYPE_INFO(0, expire, IS_LONG, 0)
ZEND_ARG_VARIADIC_INFO(0, parameters)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_get_or_set) {
	zval *zv;
	zend_string *key = NULL;
	zend_long index = 0;

	zend_fcall_info fci;
	zend_fcall_info_cache fci_cache;

	zend_long expire = 0;
	
	ts_hash_table_t *ts_ht;
	value_t v;

	ZEND_PARSE_PARAMETERS_START(3, -1)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_STR_OR_LONG(key, index)
		Z_PARAM_FUNC(fci, fci_cache)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(expire);
	#if PHP_VERSION_ID >= 80000
		Z_PARAM_VARIADIC_WITH_NAMED(fci.params, fci.param_count, fci.named_params)
	#else
		Z_PARAM_VARIADIC('*', fci.params, fci.param_count)
	#endif
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}
	
	fci.retval = return_value;

	ts_hash_table_rd_lock(ts_ht);
	if(key) {
		zend_long h = zend_get_hash_value(ZSTR_VAL(key), ZSTR_LEN(key));
		if(hash_table_quick_find(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v) == SUCCESS) {
			value_to_zval(&v, return_value);
			ts_hash_table_rd_unlock(ts_ht);
		} else {
			ts_hash_table_rd_unlock(ts_ht);
			ts_hash_table_wr_lock(ts_ht);
			if(hash_table_quick_find(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v) == SUCCESS) {
				value_to_zval(&v, return_value);
			} else {
				zend_try {
					if (zend_call_function(&fci, &fci_cache) == SUCCESS) {
						zval_to_value(return_value, &v);
						v.expire = expire;
						hash_table_quick_update(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v, NULL);
					}
				} zend_catch {
					EG(exit_status) = 0;
				} zend_end_try();
			}
			ts_hash_table_wr_unlock(ts_ht);
		}
	} else {
		if(hash_table_index_find(&ts_ht->ht, index, &v) == SUCCESS) {
			value_to_zval(&v, return_value);
			ts_hash_table_rd_unlock(ts_ht);
		} else {
			ts_hash_table_rd_unlock(ts_ht);
			ts_hash_table_wr_lock(ts_ht);
			if(hash_table_index_find(&ts_ht->ht, index, &v) == SUCCESS) {
				value_to_zval(&v, return_value);
			} else {
				zend_try {
					if (zend_call_function(&fci, &fci_cache) == SUCCESS) {
						zval_to_value(return_value, &v);
						v.expire = expire;
						hash_table_index_update(&ts_ht->ht, index, &v, NULL);
					}
				} zend_catch {
					EG(exit_status) = 0;
				} zend_end_try();
			}
			ts_hash_table_wr_unlock(ts_ht);
		}
	}
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_del, 2)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_del) {
	zval *zv;
	zend_string *key = NULL;
	zend_long index = 0;
	
	ts_hash_table_t *ts_ht;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_STR_OR_LONG(key, index)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}

	ts_hash_table_wr_lock(ts_ht);
	if(key) {
		RETVAL_BOOL(hash_table_del(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key)) == SUCCESS);
	} else {
		RETVAL_BOOL(hash_table_index_del(&ts_ht->ht, index) == SUCCESS);
	}
	ts_hash_table_wr_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_inc, 3)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_INFO(0, key)
ZEND_ARG_INFO(0, val)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_inc) {
	zval *zv;
	zend_string *key = NULL;
	zend_long index = 0;
	zval *val;
	zend_bool is_null = 0;
	
	ts_hash_table_t *ts_ht;
	value_t v1,v2;

	ZEND_PARSE_PARAMETERS_START(3, 3)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_STR_OR_LONG_OR_NULL(key, index, is_null)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}

	zval_to_value(val, &v1);

	ts_hash_table_wr_lock(ts_ht);
	if(is_null) {
		RETVAL_BOOL(hash_table_next_index_insert(&ts_ht->ht, &v1, NULL) == SUCCESS);
	} else if(key) {
		zend_long h = zend_get_hash_value(ZSTR_VAL(key), ZSTR_LEN(key));
		if(hash_table_quick_find(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v2) == FAILURE) {
			if(hash_table_quick_update(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v1, NULL) == SUCCESS) {
				value_to_zval_wr(&v1, return_value);
			}
		} else {
			value_add(&v2, &v1);
			if(v2.type != HT_T) {
				if(hash_table_quick_update(&ts_ht->ht, ZSTR_VAL(key), ZSTR_LEN(key), h, &v2, NULL) == SUCCESS) {
					value_to_zval_wr(&v2, return_value);
				}
			} else RETVAL_LONG(hash_table_num_elements(v2.ptr));
		}
	} else {
		if(hash_table_index_find(&ts_ht->ht, index, &v2) == FAILURE) {
			if(hash_table_index_update(&ts_ht->ht, index, &v1, NULL) == SUCCESS) {
				value_to_zval_wr(&v1, return_value);
			}
		} else {
			value_add(&v2, &v1);
			if(v2.type != HT_T) {
				if(hash_table_index_update(&ts_ht->ht, index, &v2, NULL) == SUCCESS) {
					value_to_zval_wr(&v2, return_value);
				}
			} else RETVAL_LONG(hash_table_num_elements(v2.ptr));
		}
	}
	ts_hash_table_wr_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_count, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_count) {
	zval *zv;
	
	ts_hash_table_t *ts_ht;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_RESOURCE(zv)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}

	ts_hash_table_rd_lock(ts_ht);
	RETVAL_LONG(hash_table_num_elements(&ts_ht->ht));
	ts_hash_table_rd_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_clean, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_TYPE_INFO(0, expire, IS_LONG, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_clean) {
	zval *zv;
	zend_long expire = 0;
	
	ts_hash_table_t *ts_ht;
	int n;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(expire)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}
	
	n = (int) expire;

	ts_hash_table_wr_lock(ts_ht);
	if(expire) hash_table_apply_with_argument(&ts_ht->ht, (hash_apply_func_arg_t) hash_table_clean_ex, &n);
	RETVAL_LONG(hash_table_num_elements(&ts_ht->ht));
	if(expire == 0) hash_table_clean(&ts_ht->ht);
	ts_hash_table_wr_unlock(ts_ht);
}

ZEND_BEGIN_ARG_INFO(arginfo_ts_var_reindex, 1)
ZEND_ARG_TYPE_INFO(0, res, IS_RESOURCE, 0)
ZEND_ARG_TYPE_INFO(0, only_integer_keys, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(ts_var_reindex) {
	zval *zv;
	zend_bool only_integer_keys = 0;
	
	ts_hash_table_t *ts_ht;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_RESOURCE(zv)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(only_integer_keys)
	ZEND_PARSE_PARAMETERS_END();
	
	if ((ts_ht = (ts_hash_table_t *) zend_fetch_resource_ex(zv, PHP_TS_VAR_DESCRIPTOR, le_ts_var_descriptor)) == NULL) {
		RETURN_FALSE;
	}
	
	ts_hash_table_wr_lock(ts_ht);
	hash_table_reindex(&ts_ht->ht, only_integer_keys);
	ts_hash_table_wr_unlock(ts_ht);

	RETURN_TRUE;
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
	
	if ((sock = (php_socket *) zend_fetch_resource_ex(zv, php_sockets_le_socket_name, php_sockets_le_socket())) == NULL) {
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

void socket_import_fd(int fd, zval *return_value) {
	php_socket *sock;
	
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_socket_import_fd, 0, 0, 1)
ZEND_ARG_TYPE_INFO(0, sockfd, IS_LONG, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(socket_import_fd) {
	zend_long fd = -1;
	
	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(fd)
	ZEND_PARSE_PARAMETERS_END();
	
	socket_import_fd((int) fd, return_value);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_socket_accept_ex, 0, 0, 3)
ZEND_ARG_TYPE_INFO(0, sockfd, IS_LONG, 0)
ZEND_ARG_INFO(1, addr)
ZEND_ARG_INFO(1, port)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(socket_accept_ex) {
	zend_long sockfd = -1;
	zval *addr, *port;
	int fd;
	php_sockaddr_storage sa_storage;
	socklen_t salen = sizeof(php_sockaddr_storage);
	struct sockaddr *sa;
	struct sockaddr_in *sin;
#if HAVE_IPV6
	struct sockaddr_in6 *sin6;
	char addr6[INET6_ADDRSTRLEN+1];
#endif
	struct sockaddr_un *s_un;
	char *addr_string;

	ZEND_PARSE_PARAMETERS_START(3, 3)
		Z_PARAM_LONG(sockfd)
		Z_PARAM_ZVAL_DEREF(addr)
		Z_PARAM_ZVAL_DEREF(port)
	ZEND_PARSE_PARAMETERS_END();
	
	sa = (struct sockaddr *) &sa_storage;
	
	fd = accept((int) sockfd, sa, &salen);

	zval_ptr_dtor(addr);
	zval_ptr_dtor(port);

	if(fd == -1) {
		RETURN_FALSE;
	}
	
	switch (sa->sa_family) {
#if HAVE_IPV6
		case AF_INET6:
			sin6 = (struct sockaddr_in6 *) sa;

			inet_ntop(AF_INET6, &sin6->sin6_addr, addr6, INET6_ADDRSTRLEN);

			ZVAL_STRING(addr, addr6);
			ZVAL_LONG(port, htons(sin6->sin6_port));
			break;
#endif
		case AF_INET:
			sin = (struct sockaddr_in *) sa;
			addr_string = inet_ntoa(sin->sin_addr);

			ZVAL_STRING(addr, addr_string);
			ZVAL_LONG(port, htons(sin->sin_port));
			break;

		case AF_UNIX:
			s_un = (struct sockaddr_un *) sa;

			ZVAL_STRING(addr, s_un->sun_path);
			ZVAL_LONG(port, 0);
			break;

		default:
			RETURN_FALSE;
	}

	socket_import_fd(fd, return_value);
}

// ===========================================================================================================

#ifdef MYSQLI_USE_MYSQLND
#include <mysqli/php_mysqli_structs.h>

ZEND_BEGIN_ARG_INFO_EX(arginfo_mysqli_export_fd, 0, 0, 1)
ZEND_ARG_OBJ_INFO(0, mysql, mysqli, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(mysqli_export_fd) {
	MY_MYSQL *mysql;
	zval *mysql_link;
	php_stream *stream = NULL;
	php_socket_t this_fd;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "O", &mysql_link, mysqli_link_class_entry) == FAILURE) {
		#if PHP_VERSION_ID < 80000
		RETURN_FALSE;
		#else
		RETURN_THROWS();
		#endif
	}
	
	MYSQLI_FETCH_RESOURCE_CONN(mysql, mysql_link, MYSQLI_STATUS_VALID);
	
	stream = mysql->mysql->data->vio->data->m.get_stream(mysql->mysql->data->vio);
	if(stream != NULL && SUCCESS == php_stream_cast(stream, PHP_STREAM_AS_FD_FOR_SELECT | PHP_STREAM_CAST_INTERNAL, (void*)&this_fd, 1) && ZEND_VALID_SOCKET(this_fd)) {
		RETURN_LONG(this_fd);
	} else {
		RETURN_FALSE;
	}
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_mysqli_stmt_async_execute, 0, 0, 1)
ZEND_ARG_OBJ_INFO(0, statement, mysqli_stmt, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(mysqli_stmt_async_execute) {
	MY_STMT *stmt;
	zval *mysql_stmt;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "O", &mysql_stmt, mysqli_stmt_class_entry) == FAILURE) {
		#if PHP_VERSION_ID < 80000
		RETURN_FALSE;
		#else
		RETURN_THROWS();
		#endif
	}
	
	MYSQLI_FETCH_RESOURCE_STMT(stmt, mysql_stmt, MYSQLI_STATUS_VALID);
	
	if(FAIL == stmt->stmt->m->send_execute(stmt->stmt, MYSQLND_SEND_EXECUTE_IMPLICIT, NULL, NULL)) {
		RETURN_FALSE;
	} else {
		RETURN_TRUE;
	}
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_mysqli_stmt_reap_async_query, 0, 0, 1)
ZEND_ARG_OBJ_INFO(0, statement, mysqli_stmt, 0)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(mysqli_stmt_reap_async_query) {
	MY_STMT *stmt;
	zval *mysql_stmt;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "O", &mysql_stmt, mysqli_stmt_class_entry) == FAILURE) {
		#if PHP_VERSION_ID < 80000
		RETURN_FALSE;
		#else
		RETURN_THROWS();
		#endif
	}
	
	MYSQLI_FETCH_RESOURCE_STMT(stmt, mysql_stmt, MYSQLI_STATUS_VALID);
	
	if(FAIL == stmt->stmt->m->parse_execute_response(stmt->stmt, MYSQLND_PARSE_EXEC_RESPONSE_IMPLICIT)) {
		RETURN_FALSE;
	} else {
		RETURN_TRUE;
	}
}

#endif

// ===========================================================================================================

#ifndef GC_PROTECT_RECURSION
#define GC_PROTECT_RECURSION(ht) (ht)->u.v.nApplyCount++
#endif
#ifndef GC_UNPROTECT_RECURSION
#define GC_UNPROTECT_RECURSION(ht) (ht)->u.v.nApplyCount--
#endif
#ifndef Z_IS_RECURSIVE_P
#define Z_IS_RECURSIVE_P(val) Z_ARRVAL_P(val)->u.v.nApplyCount > 0
#endif
#ifndef ZEND_CONSTANT_SET_FLAGS
#define ZEND_CONSTANT_SET_FLAGS(c,f,fx) do {\
	(c)->flags = f;\
	(c)->module_number = fx;\
} while(0)
#endif

static int validate_constant_array(HashTable *ht) /* {{{ */
{
	int ret = 1;
	zval *val;

	GC_PROTECT_RECURSION(ht);
	ZEND_HASH_FOREACH_VAL_IND(ht, val) {
		ZVAL_DEREF(val);
		if (Z_REFCOUNTED_P(val)) {
			if (Z_TYPE_P(val) == IS_ARRAY) {
				if (Z_REFCOUNTED_P(val)) {
					if (Z_IS_RECURSIVE_P(val)) {
						zend_error(E_WARNING, "Constants cannot be recursive arrays");
						ret = 0;
						break;
					} else if (!validate_constant_array(Z_ARRVAL_P(val))) {
						ret = 0;
						break;
					}
				}
			} else if (Z_TYPE_P(val) != IS_STRING && Z_TYPE_P(val) != IS_RESOURCE) {
				zend_error(E_WARNING, "Constants may only evaluate to scalar values, arrays or resources");
				ret = 0;
				break;
			}
		}
	} ZEND_HASH_FOREACH_END();
	GC_UNPROTECT_RECURSION(ht);
	return ret;
}
/* }}} */

static void copy_constant_array(zval *dst, zval *src) /* {{{ */
{
	zend_string *key;
	zend_ulong idx;
	zval *new_val, *val;

	array_init_size(dst, zend_hash_num_elements(Z_ARRVAL_P(src)));
	ZEND_HASH_FOREACH_KEY_VAL_IND(Z_ARRVAL_P(src), idx, key, val) {
		/* constant arrays can't contain references */
		ZVAL_DEREF(val);
		if (key) {
			new_val = zend_hash_add_new(Z_ARRVAL_P(dst), key, val);
		} else {
			new_val = zend_hash_index_add_new(Z_ARRVAL_P(dst), idx, val);
		}
		if (Z_TYPE_P(val) == IS_ARRAY) {
			if (Z_REFCOUNTED_P(val)) {
				copy_constant_array(new_val, val);
			}
		} else {
			Z_TRY_ADDREF_P(val);
		}
	} ZEND_HASH_FOREACH_END();
}
/* }}} */

ZEND_BEGIN_ARG_INFO_EX(arginfo_redefine, 0, 0, 2)
	ZEND_ARG_INFO(0, constant_name)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, case_insensitive)
ZEND_END_ARG_INFO()

PHP_FUNCTION(redefine) /* {{{ */
{
	zend_string *name;
	zval *val, val_free;
	zend_bool non_cs = 0;
	int case_sensitive = CONST_CS;
	zend_constant c;

	ZEND_PARSE_PARAMETERS_START(2, 3)
		Z_PARAM_STR(name)
		Z_PARAM_ZVAL(val)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(non_cs)
	ZEND_PARSE_PARAMETERS_END();

	if (non_cs) {
		case_sensitive = 0;
	}

	if (zend_memnstr(ZSTR_VAL(name), "::", sizeof("::") - 1, ZSTR_VAL(name) + ZSTR_LEN(name))) {
		zend_error(E_WARNING, "Class constants cannot be defined or redefined");
		RETURN_FALSE;
	}

	ZVAL_UNDEF(&val_free);

#if PHP_VERSION_ID < 80000
repeat:
#endif
	switch (Z_TYPE_P(val)) {
		case IS_LONG:
		case IS_DOUBLE:
		case IS_STRING:
		case IS_FALSE:
		case IS_TRUE:
		case IS_NULL:
		case IS_RESOURCE:
			break;
		case IS_ARRAY:
			if (Z_REFCOUNTED_P(val)) {
				if (!validate_constant_array(Z_ARRVAL_P(val))) {
					RETURN_FALSE;
				} else {
					copy_constant_array(&c.value, val);
					goto register_constant;
				}
			}
			break;
		case IS_OBJECT:
		#if PHP_VERSION_ID >= 80000
			if (Z_OBJ_HT_P(val)->cast_object(Z_OBJ_P(val), &val_free, IS_STRING) == SUCCESS) {
				val = &val_free;
				break;
			}
		#else
			if (Z_TYPE(val_free) == IS_UNDEF) {
				if (Z_OBJ_HT_P(val)->get) {
					val = Z_OBJ_HT_P(val)->get(val, &val_free);
					goto repeat;
				} else if (Z_OBJ_HT_P(val)->cast_object) {
					if (Z_OBJ_HT_P(val)->cast_object(val, &val_free, IS_STRING) == SUCCESS) {
						val = &val_free;
						break;
					}
				}
			}
		#endif
			/* no break */
		default:
			zend_error(E_WARNING, "Constants may only evaluate to scalar values, arrays or resources");
			zval_ptr_dtor(&val_free);
			RETURN_FALSE;
	}

	ZVAL_COPY(&c.value, val);
	zval_ptr_dtor(&val_free);

register_constant:
	if (non_cs) {
		zend_error(E_DEPRECATED,
			"define(): Declaration of case-insensitive constants is deprecated");
	}

	zval *zv = zend_get_constant(name);
	if(zv) {
		ZVAL_COPY(zv, val);
		RETURN_TRUE;
	}

	/* non persistent */
	ZEND_CONSTANT_SET_FLAGS(&c, case_sensitive, PHP_USER_CONSTANT);
	c.name = zend_string_copy(name);
	if (zend_register_constant(&c) == SUCCESS) {
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}
}

// ===========================================================================================================

static const zend_function_entry ext_functions[] = {
	ZEND_FE(create_task, arginfo_create_task)
	ZEND_FE(task_kill, arginfo_task_kill)
	ZEND_FE(task_join, arginfo_task_join)
	ZEND_FE(task_is_run, arginfo_task_is_run)
	ZEND_FE(task_wait, arginfo_task_wait)
	ZEND_FE(task_get_delay, arginfo_task_get_delay)
	ZEND_FE(task_set_delay, arginfo_task_set_delay)
	ZEND_FE(task_get_num, arginfo_task_get_num)
	PHP_FALIAS(task_get_threads, task_get_num, arginfo_task_get_num)
	ZEND_FE(task_set_threads, arginfo_task_set_threads)
	ZEND_FE(task_get_debug, arginfo_task_get_debug)
	ZEND_FE(task_set_debug, arginfo_task_set_debug)
	ZEND_FE(task_get_run, arginfo_task_get_run)
	ZEND_FE(task_set_run, arginfo_task_set_run)
	ZEND_FE(pthread_sigmask, arginfo_pthread_sigmask)
	ZEND_FE(pthread_yield, arginfo_pthread_yield)

	ZEND_FE(go, arginfo_go)
	ZEND_FE(call_and_free_shutdown, arginfo_call_and_free_shutdown)
	ZEND_FE(redefine, arginfo_redefine)

	ZEND_FE(set_timeout, arginfo_set_timeout)
	ZEND_FE(clear_timeout, arginfo_clear_timeout)
	ZEND_FE(trigger_timeout, arginfo_trigger_timeout)

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

	PHP_FE(ts_var_declare, arginfo_ts_var_declare)
	PHP_FE(ts_var_fd, arginfo_ts_var_fd)
	PHP_FE(ts_var_expire, arginfo_ts_var_expire)
	PHP_FE(ts_var_exists, arginfo_ts_var_exists)
	PHP_FE(ts_var_set, arginfo_ts_var_set)
	PHP_FALIAS(ts_var_put, ts_var_set, arginfo_ts_var_set)
	PHP_FE(ts_var_push, arginfo_ts_var_push)
	PHP_FE(ts_var_pop, arginfo_ts_var_pop)
	PHP_FE(ts_var_shift, arginfo_ts_var_shift)
	PHP_FE(ts_var_minmax, arginfo_ts_var_minmax)
	PHP_FE(ts_var_get, arginfo_ts_var_get)
	PHP_FE(ts_var_get_or_set, arginfo_ts_var_get_or_set)
	PHP_FE(ts_var_del, arginfo_ts_var_del)
	PHP_FE(ts_var_inc, arginfo_ts_var_inc)
	PHP_FE(ts_var_count, arginfo_ts_var_count)
	PHP_FE(ts_var_clean, arginfo_ts_var_clean)
	PHP_FE(ts_var_reindex, arginfo_ts_var_reindex)
	
	PHP_FE(socket_export_fd, arginfo_socket_export_fd)
	PHP_FE(socket_import_fd, arginfo_socket_import_fd)
	PHP_FE(socket_accept_ex, arginfo_socket_accept_ex)

#ifdef MYSQLI_USE_MYSQLND
	PHP_FE(mysqli_export_fd, arginfo_mysqli_export_fd)
	PHP_FE(mysqli_stmt_async_execute, arginfo_mysqli_stmt_async_execute)
	PHP_FE(mysqli_stmt_reap_async_query, arginfo_mysqli_stmt_reap_async_query)
#endif
	
	{NULL, NULL, NULL}
};

static const zend_function_entry spl_ce_GoExitException_methods[] = {
	PHP_ME(spl_ce_GoExitException, getStatus, arginfo_spl_ce_GoExitException_getStatus, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

// -----------------------------------------------------------------------------------------------------------

static void php_threadtask_globals_ctor(php_threadtask_globals_struct *php_threadtask_globals) {
	php_threadtask_globals->timestamp = 0;
	php_threadtask_globals->timeout = NULL;
	php_threadtask_globals->is_throw_exit = 0;
}

static void php_threadtask_globals_dtor(php_threadtask_globals_struct *php_threadtask_globals) {
	if(php_threadtask_globals->timeout) {
		timeout_t *t = php_threadtask_globals->timeout;

		pthread_mutex_lock(&tlock);
		if(thead == t) {
			if(t->next == t) {
				thead = NULL;
			} else {
				thead = t->next;
				thead->prev = thead->prev->prev;
				thead->prev->next = thead;
			}
		} else {
			t->prev = t;
			t->next = t;
			thead = t;
		}
		pthread_mutex_unlock(&tlock);
		
		free(t);

		php_threadtask_globals->timeout = NULL;
	}
}

static void php_destroy_threadtask(zend_resource *rsrc) {
	wait_t *ptr = (wait_t *) rsrc->ptr;
	
	sem_destroy(&ptr->sem);
	
	free(ptr);
	
	dprintf("RESOURCE %p destroy(task status)\n", ptr);
}

static void php_destroy_ts_var(zend_resource *rsrc) {
	ts_hash_table_destroy(rsrc->ptr);
	
	dprintf("RESOURCE %p destroy(ts var)\n", rsrc->ptr);
}

#define spl_ce_Exception zend_ce_exception

#if PHP_VERSION_ID >= 80100

#define REGISTER_SPL_SUB_CLASS_EX(class_name, parent_class_name, obj_ctor, funcs) \
	spl_register_sub_class(&spl_ce_ ## class_name, spl_ce_ ## parent_class_name, # class_name, obj_ctor, funcs);

void spl_register_sub_class(zend_class_entry ** ppce, zend_class_entry * parent_ce, char * class_name, void *obj_ctor, const zend_function_entry * function_list) {
	zend_class_entry ce;

	INIT_CLASS_ENTRY_EX(ce, class_name, strlen(class_name), function_list);
	*ppce = zend_register_internal_class_ex(&ce, parent_ce);

	/* entries changed by initialize */
	if (obj_ctor) {
		(*ppce)->create_object = obj_ctor;
	} else {
		(*ppce)->create_object = parent_ce->create_object;
	}
}
#endif

static PHP_MINIT_FUNCTION(threadtask) {
	ts_allocate_id(&php_threadtask_globals_id, sizeof(php_threadtask_globals_struct), (ts_allocate_ctor) php_threadtask_globals_ctor, (ts_allocate_dtor) php_threadtask_globals_dtor);
	le_threadtask_descriptor = zend_register_list_destructors_ex(php_destroy_threadtask, NULL, PHP_THREADTASK_DESCRIPTOR, module_number);
	le_ts_var_descriptor = zend_register_list_destructors_ex(php_destroy_ts_var, NULL, PHP_TS_VAR_DESCRIPTOR, module_number);
	
	REGISTER_SPL_SUB_CLASS_EX(GoExitException, Exception, NULL, spl_ce_GoExitException_methods);
	zend_declare_property_long(spl_ce_GoExitException, ZEND_STRL("status"), 0, ZEND_ACC_PRIVATE);
	zend_set_user_opcode_handler(ZEND_EXIT, go_exit_handler);

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

