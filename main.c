#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <embed/php_embed.h>
#include "func.h"

static int php_threadtask_startup(sapi_module_struct *sapi_module)
{
	php_embed_module.additional_functions = additional_functions;
	if (php_module_startup(sapi_module, NULL, 0)==FAILURE) {
		return FAILURE;
	}
	return SUCCESS;
}

int main(int argc, char *argv[]) {
	char *primary_script;
	zend_file_handle file_handle;

	if(argc < 2) {
		fprintf(stderr, "usage: %s phpfile\n", argv[0]);
		return 255;
	}

	php_embed_module.startup = php_threadtask_startup;

	if(php_embed_init(argc, argv) == FAILURE) {
		fprintf(stderr, "php_embed_init failure\n");
		return 1;
	}

	primary_script = estrdup(argv[1]);

	zend_stream_init_filename(&file_handle, primary_script);
	php_execute_script(&file_handle);

	php_embed_shutdown();
	return 0;
}
