#ifndef _FUNC_H
#define _FUNC_H

#include <php.h>

extern const zend_function_entry additional_functions[];

void thread_init();
void thread_destroy();

#endif
