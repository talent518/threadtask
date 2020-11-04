#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <embed/php_embed.h>
#include "func.h"

static int php_threadtask_startup(sapi_module_struct *sapi_module)
{
#ifndef SAPI_NAME
	php_embed_module.name = "cli";
#else
	php_embed_module.name = SAPI_NAME;
#endif
	php_embed_module.additional_functions = additional_functions;
	if (php_module_startup(sapi_module, NULL, 0)==FAILURE) {
		return FAILURE;
	}
	return SUCCESS;
}

int main(int argc, char *argv[]) {
	zend_file_handle file_handle;

	if(argc < 2) {
		fprintf(stderr, "usage: %s <phpfile>\n", argv[0]);
		return 255;
	}

	php_embed_module.startup = php_threadtask_startup;

	if(php_embed_init(argc, argv) == FAILURE) {
		fprintf(stderr, "php_embed_init failure\n");
		return 1;
	}

	thread_init();

	CG(skip_shebang) = 1;

	zend_stream_init_filename(&file_handle, argv[1]);
	php_execute_script(&file_handle);

	php_embed_shutdown();
	thread_destroy();
	return 0;
}
