#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <embed/php_embed.h>
#include <standard/php_var.h>
#include "func.h"

void cli_register_file_handles(void) {
	php_stream *s_in, *s_out, *s_err;
	php_stream_context *sc_in=NULL, *sc_out=NULL, *sc_err=NULL;
	zend_constant ic, oc, ec;

	s_in  = php_stream_open_wrapper_ex("php://stdin",  "rb", 0, NULL, sc_in);
	s_out = php_stream_open_wrapper_ex("php://stdout", "wb", 0, NULL, sc_out);
	s_err = php_stream_open_wrapper_ex("php://stderr", "wb", 0, NULL, sc_err);

	if (s_in==NULL || s_out==NULL || s_err==NULL) {
		if (s_in) php_stream_close(s_in);
		if (s_out) php_stream_close(s_out);
		if (s_err) php_stream_close(s_err);
		return;
	}

#if PHP_DEBUG
	/* do not close stdout and stderr */
	s_out->flags |= PHP_STREAM_FLAG_NO_CLOSE;
	s_err->flags |= PHP_STREAM_FLAG_NO_CLOSE;
#endif

	php_stream_to_zval(s_in,  &ic.value);
	php_stream_to_zval(s_out, &oc.value);
	php_stream_to_zval(s_err, &ec.value);

	ZEND_CONSTANT_SET_FLAGS(&ic, CONST_CS, 0);
	ic.name = zend_string_init_interned("STDIN", sizeof("STDIN")-1, 0);
	zend_register_constant(&ic);

	ZEND_CONSTANT_SET_FLAGS(&oc, CONST_CS, 0);
	oc.name = zend_string_init_interned("STDOUT", sizeof("STDOUT")-1, 0);
	zend_register_constant(&oc);

	ZEND_CONSTANT_SET_FLAGS(&ec, CONST_CS, 0);
	ec.name = zend_string_init_interned("STDERR", sizeof("STDERR")-1, 0);
	zend_register_constant(&ec);
}

static void sapi_cli_register_variables(zval *var) {
	char *filename = SG(request_info).path_translated;

	//printf("filename = %s\n", filename);

	php_import_environment_variables(var);

	php_register_variable("PHP_SELF", filename, var);
	php_register_variable("SCRIPT_NAME", filename, var);
	php_register_variable("SCRIPT_FILENAME", filename, var);
	php_register_variable("PATH_TRANSLATED", filename, var);
	php_register_variable("DOCUMENT_ROOT", "", var);

	//php_var_dump(var, 0);
}

static int php_threadtask_startup(sapi_module_struct *sapi_module)
{
	php_embed_module.additional_functions = additional_functions;
	if (php_module_startup(sapi_module, NULL, 0)==FAILURE) {
		return FAILURE;
	}
	return SUCCESS;
}

volatile zend_bool isReload = 0;
int main(int argc, char *argv[]) {
	zend_file_handle file_handle;

	if(argc < 2) {
		fprintf(stderr, "usage: %s <phpfile>\n", argv[0]);
		return 255;
	}

	thread_init();

#ifndef SAPI_NAME
	php_embed_module.name = "cli";
#else
	php_embed_module.name = SAPI_NAME;
#endif

	php_embed_module.startup = php_threadtask_startup;
	php_embed_module.register_server_variables = sapi_cli_register_variables;

	old_ub_write_handler = php_embed_module.ub_write;
	old_flush_handler = php_embed_module.flush;

	php_embed_module.ub_write = php_thread_ub_write_handler;
	php_embed_module.flush = php_thread_flush_handler;

	if(php_embed_init(argc-1, argv+1) == FAILURE) {
		fprintf(stderr, "php_embed_init failure\n");
		return 1;
	}

	cli_register_file_handles();

	CG(skip_shebang) = 1;

	SG(request_info).path_translated = argv[1];

	zend_first_try {
		zend_stream_init_filename(&file_handle, argv[1]);
		php_execute_script(&file_handle);
	} zend_catch {
		if(EG(exit_status)) {
			isReload = 1;
		}
	} zend_end_try();

	if(isReload) {
		printf("\n\nreload\n\n");
		execv(argv[0], argv);
		{
			char **args = (char**) malloc(sizeof(char*)*(argc+1));
			memcpy(args, argv, sizeof(char*)*argc);
			args[argc] = NULL;
			execv(argv[0], args);
		}
	}

	php_embed_shutdown();

	thread_destroy();

	return 0;
}
