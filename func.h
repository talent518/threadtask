#ifndef _FUNC_H
#define _FUNC_H

#include <php.h>

extern const zend_function_entry additional_functions[];

void thread_init();
void thread_running();
void thread_destroy();

extern size_t (*old_ub_write_handler)(const char *str, size_t str_length);
extern void (*old_flush_handler)(void *server_context);

size_t php_thread_ub_write_handler(const char *str, size_t str_length);
void php_thread_flush_handler(void *server_context);

#endif
