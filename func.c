#include "func.h"

ZEND_BEGIN_ARG_INFO(arginfo_create_task, 0)
	ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

static PHP_FUNCTION(create_task) {
	char *filename;
	size_t filename_len;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STRING(filename, filename_len)
	ZEND_PARSE_PARAMETERS_END();

	RETURN_STRINGL(filename, filename_len);
}

const zend_function_entry additional_functions[] = {
	ZEND_FE(create_task, arginfo_create_task)
	{NULL, NULL, NULL}
};
