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

extern const zend_function_entry additional_functions[];
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

#endif
