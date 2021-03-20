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

#if PHP_VERSION_ID < 70400
void zend_stream_init_filename(zend_file_handle *file_handle, const char *script_file);
#endif

#ifndef Z_PARAM_RESOURCE_OR_NULL
	#define Z_PARAM_RESOURCE_OR_NULL(dest) \
		Z_PARAM_RESOURCE_EX(dest, 1, 0)
#endif

#ifndef Z_PARAM_ZVAL_DEREF
	#define Z_PARAM_ZVAL_DEREF(dest) Z_PARAM_ZVAL_EX2(dest, 0, 1, 0)
#endif

#ifndef Z_PARAM_STR_OR_LONG_EX
	#if PHP_VERSION_ID >= 70400
		#define ERROR_CODE _error_code
	#else
		#define ERROR_CODE error_code
	#endif

	#define Z_PARAM_STR_OR_LONG_EX(dest_str, dest_long, is_null, allow_null) \
		Z_PARAM_PROLOGUE(0, 0); \
		if (UNEXPECTED(!zend_parse_arg_str_or_long(_arg, &dest_str, &dest_long, &is_null, allow_null))) { \
			_expected_type = Z_EXPECTED_STRING; \
			ERROR_CODE = ZPP_ERROR_WRONG_ARG; \
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

#ifndef ZEND_PARSE_PARAMETERS_NONE
	#define ZEND_PARSE_PARAMETERS_NONE() do { \
		if (UNEXPECTED(ZEND_NUM_ARGS() != 0)) { \
			zend_wrong_parameters_none_error(); \
			return; \
		} \
	} while (0)

	static zend_always_inline int zend_wrong_parameters_none_error(void) /* {{{ */
	{
		int num_args = ZEND_CALL_NUM_ARGS(EG(current_execute_data));
		zend_function *active_function = EG(current_execute_data)->func;
		const char *class_name = active_function->common.scope ? ZSTR_VAL(active_function->common.scope->name) : "";

		zend_internal_argument_count_error(
		            ZEND_ARG_USES_STRICT_TYPES(),
		            "%s%s%s() expects %s %d parameter%s, %d given",
		            class_name, \
		            class_name[0] ? "::" : "", \
		            ZSTR_VAL(active_function->common.function_name),
		            "exactly",
		            0,
		            "s",
		            num_args);
		return FAILURE;
	}
/* }}} */

#endif

#endif
