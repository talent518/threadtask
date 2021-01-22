#ifndef _FUNC_H
#define _FUNC_H

#include <php.h>

struct pthread_fake {
	void *nothing[90];
	pid_t tid;
	pid_t pid;
};

#define pthread_tid_ex(t) ((struct pthread_fake*) t)->tid
#define pthread_tid pthread_tid_ex(pthread_self())

#define dprintf(fmt, args...) if(UNEXPECTED(isDebug)) {fprintf(stderr, "[%s] [TID:%d] " fmt, gettimeofstr(), pthread_tid, ##args);fflush(stderr);}

extern zend_module_entry threadtask_module_entry;
extern volatile unsigned int maxthreads;
extern volatile unsigned int delay;
extern volatile zend_bool isDebug;
extern volatile zend_bool isReload;

void thread_init();
void thread_running();
void thread_destroy();

const char *gettimeofstr();

extern size_t (*old_ub_write_handler)(const char *str, size_t str_length);
extern void (*old_flush_handler)(void *server_context);

size_t php_thread_ub_write_handler(const char *str, size_t str_length);
void php_thread_flush_handler(void *server_context);

void socket_import_fd(int fd, zval *return_value);

#ifndef Z_PARAM_RESOURCE_OR_NULL
	#define Z_PARAM_RESOURCE_OR_NULL(dest) \
		Z_PARAM_RESOURCE_EX(dest, 1, 0)
#endif

#ifndef Z_PARAM_STR_OR_LONG_EX
	#define Z_PARAM_STR_OR_LONG_EX(dest_str, dest_long, is_null, allow_null) \
		Z_PARAM_PROLOGUE(0, 0); \
		if (UNEXPECTED(!zend_parse_arg_str_or_long(_arg, &dest_str, &dest_long, &is_null, allow_null))) { \
			_expected_type = Z_EXPECTED_STRING; \
			_error_code = ZPP_ERROR_WRONG_ARG; \
			break; \
		}

	#define Z_PARAM_STR_OR_LONG(dest_str, dest_long) \
		Z_PARAM_STR_OR_LONG_EX(dest_str, dest_long, _dummy, 0);

	#define Z_PARAM_STR_OR_LONG_OR_NULL(dest_str, dest_long, is_null) \
		Z_PARAM_STR_OR_LONG_EX(dest_str, dest_long, is_null, 1);

	static zend_always_inline int zend_parse_arg_str_or_long(zval *arg, zend_string **dest_str, zend_long *dest_long,
		zend_bool *is_null, int allow_null)
	{
		if (allow_null) {
			*is_null = 0;
		}
		if (EXPECTED(Z_TYPE_P(arg) == IS_STRING)) {
			*dest_str = Z_STR_P(arg);
			return 1;
		} else if (EXPECTED(Z_TYPE_P(arg) == IS_LONG)) {
			*dest_str = NULL;
			*dest_long = Z_LVAL_P(arg);
			return 1;
		} else if (allow_null && EXPECTED(Z_TYPE_P(arg) == IS_NULL)) {
			*dest_str = NULL;
			*is_null = 1;
			return 1;
		} else {
			if (zend_parse_arg_long_weak(arg, dest_long)) {
				*dest_str = NULL;
				return 1;
			} else if (zend_parse_arg_str_weak(arg, dest_str)) {
				*dest_long = 0;
				return 1;
			} else {
				return 0;
			}
		}
	}
#endif

#endif
