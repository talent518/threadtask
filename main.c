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

int main(int argc, char *argv[]) {
	zend_file_handle file_handle;
	int opt;

opts:
	while((opt = getopt(argc, argv, "Dd:t:r")) != -1) {
		switch(opt) {
			case 'D':
				isDebug = 1;
				break;
			case 'd':
				delay = atoi(optarg);
				if(delay < 1) delay = 1;
				break;
			case 't':
				maxthreads = atoi(optarg);
				if(maxthreads < 1) maxthreads = 1;
				break;
			case 'r':
				isReload = 1;
				break;
			case 'h':
			case '?':
			default:
				goto usage;
		}
	}
	
	if(optind >= argc) {
		goto usage;
	}
	
	if(access(argv[optind], F_OK|R_OK) != 0) {
		perror("phpfile is not exists");
		return 1;
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

	if(php_embed_init(argc-optind, argv+optind) == FAILURE) {
		fprintf(stderr, "php_embed_init failure\n");
		return 1;
	}

	cli_register_file_handles();

	thread_running();

	CG(skip_shebang) = 1;

	SG(request_info).path_translated = argv[optind];
	
	zend_register_long_constant(ZEND_STRL("THREAD_TASK_NUM"), maxthreads, CONST_CS, PHP_USER_CONSTANT);
	zend_register_long_constant(ZEND_STRL("THREAD_TASK_DELAY"), delay, CONST_CS, PHP_USER_CONSTANT);

	dprintf("BEGIN THREADTASK\n");

	zend_first_try {
		zend_stream_init_filename(&file_handle, argv[optind]);
		php_execute_script(&file_handle);
	} zend_catch {
		if(EG(exit_status)) {
			isReload = 1;
		}
	} zend_end_try();

	if(isReload) {
		dprintf("RELOAD THREADTASK\n");
		char **args = (char**) malloc(sizeof(char*)*(argc+1));
		memcpy(args, argv, sizeof(char*)*argc);
		args[argc] = NULL;
		execv(argv[0], args);
	}

	dprintf("END THREADTASK\n");

	php_embed_shutdown();

	thread_destroy();

	return 0;
usage:
	fprintf(stderr, 
		"usage: %s [-D] [-d <delay>] [-t <threads>] [-r] [--] <phpfile> args...\n"
		"    -D              Debug info\n"
		"    -d <delay>      Delay seconds\n"
		"    -t <threads>    Max threads\n"
		"    -r              Auto reload\n"
		, argv[0]);
	return 255;
}
