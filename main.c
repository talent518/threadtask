#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <zend.h>
#include <zend_extensions.h>
#include <embed/php_embed.h>
#include <standard/php_var.h>
#include <standard/info.h>

#include "func.h"

#ifndef ZEND_CONSTANT_SET_FLAGS
	#define ZEND_CONSTANT_SET_FLAGS(z,c,n) do { \
			(z)->flags = c; \
			(z)->module_number = n; \
		} while(0)
#endif

#if PHP_VERSION_ID < 70400
void zend_stream_init_filename(zend_file_handle *file_handle, const char *script_file) {
    int c;

	memset(file_handle, 0, sizeof(zend_file_handle));
    file_handle->type = ZEND_HANDLE_FP;
    file_handle->opened_path = NULL;
    file_handle->free_filename = 0;
    if (!(file_handle->handle.fp = VCWD_FOPEN(script_file, "rb"))) {
        fprintf(stderr, "Could not open input file: %s\n", script_file);
        zend_bailout();
        return;
    }
    file_handle->filename = script_file;

    CG(start_lineno) = 1; 

    /* #!php support */
    c = fgetc(file_handle->handle.fp);
    if (c == '#' && (c = fgetc(file_handle->handle.fp)) == '!') {
        while (c != '\n' && c != '\r' && c != EOF) {
            c = fgetc(file_handle->handle.fp);  /* skip to end of line */
        }
        /* handle situations where line is terminated by \r\n */
        if (c == '\r') {
            if (fgetc(file_handle->handle.fp) != '\n') {
                zend_long pos = zend_ftell(file_handle->handle.fp);
                zend_fseek(file_handle->handle.fp, pos - 1, SEEK_SET);
            }
        }
        CG(start_lineno) = 2; 
    } else {
        rewind(file_handle->handle.fp);
    }
}
#endif

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

	if(!filename) filename = "";

	//printf("filename = %s\n", filename);

	php_import_environment_variables(var);

	php_register_variable("PHP_SELF", filename, var);
	php_register_variable("SCRIPT_NAME", filename, var);
	php_register_variable("SCRIPT_FILENAME", filename, var);
	php_register_variable("PATH_TRANSLATED", filename, var);
	php_register_variable("DOCUMENT_ROOT", "", var);

	//php_var_dump(var, 0);
}

static int php_threadtask_startup(sapi_module_struct *sapi_module) {
	if (php_module_startup(sapi_module, &threadtask_module_entry, 1) == FAILURE) {
		return FAILURE;
	}
	
	return SUCCESS;
}

static int module_name_cmp(Bucket *f, Bucket *s) {
	return strcasecmp(((zend_module_entry *)Z_PTR(f->val))->name, ((zend_module_entry *)Z_PTR(s->val))->name);
}

static void print_modules(void) {
	HashTable sorted_registry;
	zend_module_entry *module;

	zend_hash_init(&sorted_registry, 50, NULL, NULL, 0);
	zend_hash_copy(&sorted_registry, &module_registry, NULL);
	zend_hash_sort(&sorted_registry, module_name_cmp, 0);
	ZEND_HASH_FOREACH_PTR(&sorted_registry, module) {
		php_printf("%s\n", module->name);
	} ZEND_HASH_FOREACH_END();
	zend_hash_destroy(&sorted_registry);
}

static void print_extension_info(zend_extension *ext) {
	php_printf("%s\n", ext->name);
}

static int extension_name_cmp(const zend_llist_element **f, const zend_llist_element **s) {
	zend_extension *fe = (zend_extension*)(*f)->data;
	zend_extension *se = (zend_extension*)(*s)->data;
	return strcmp(fe->name, se->name);
}

static void print_extensions(void) {
	zend_llist sorted_exts;

	zend_llist_copy(&sorted_exts, &zend_extensions);
	sorted_exts.dtor = NULL;
	zend_llist_sort(&sorted_exts, extension_name_cmp);
	zend_llist_apply(&sorted_exts, (llist_apply_func_t) print_extension_info);
	zend_llist_destroy(&sorted_exts);
}

int main(int argc, char *argv[]) {
	zend_file_handle file_handle;
	int opt;
	char path[PATH_MAX];
	size_t sz = readlink("/proc/self/exe", path, PATH_MAX);
	path[sz] = '\0';
	char *ini_path_override = NULL;
	int is_module_list = 0, is_print_info = 0;

	while((opt = getopt(argc, argv, "Dd:t:rc:mivh?")) != -1) {
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
			case 'c':
				ini_path_override = optarg;
				break;
			case 'm':
				is_module_list = 1;
				break;
			case 'i':
				is_print_info = 1;
				break;
			case 'v':
				printf("%s\n", PHP_VERSION);
				return 0;
				break;
			case 'h':
			case '?':
			default:
				goto usage;
		}
	}
	
	if(is_module_list == 0 && is_print_info == 0 && optind >= argc) {
		goto usage;
	}

	thread_init();

	php_embed_module.executable_location = path;
	php_embed_module.php_ini_path_override = ini_path_override;

#ifndef SAPI_NAME
	php_embed_module.name = "cli";
#else
	php_embed_module.name = SAPI_NAME;
#endif

	php_embed_module.startup = php_threadtask_startup;
	php_embed_module.register_server_variables = sapi_cli_register_variables;
	php_embed_module.phpinfo_as_text = is_print_info;

	old_ub_write_handler = php_embed_module.ub_write;
	old_flush_handler = php_embed_module.flush;

	php_embed_module.ub_write = php_thread_ub_write_handler;
	php_embed_module.flush = php_thread_flush_handler;

	if(php_embed_init(argc-optind, argv+optind) == FAILURE) {
		fprintf(stderr, "php_embed_init failure\n");
		return 1;
	}

	zend_register_string_constant(ZEND_STRL("THREAD_TASK_NAME"), "main", CONST_CS, PHP_USER_CONSTANT);

	cli_register_file_handles();

	if(is_module_list) {
		php_printf("[PHP Modules]\n");
		print_modules();
		php_printf("\n[Zend Modules]\n");
		print_extensions();
		php_printf("\n");
		php_output_end_all();
		EG(exit_status) = 0;
		goto out;
	}

	if(is_print_info) {
		php_print_info(PHP_INFO_ALL & ~PHP_INFO_CREDITS);
		php_output_end_all();
		EG(exit_status) = 0;
		goto out;
	}

	thread_running();

#if PHP_VERSION_ID >= 70400
	CG(skip_shebang) = 1;
#endif

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

	dprintf("END THREADTASK\n");

	if(isReload) {
		dprintf("RELOAD THREADTASK\n");
		char **args = (char**) malloc(sizeof(char*)*(argc+1));
		memcpy(args, argv, sizeof(char*)*argc);
		args[argc] = NULL;
		execv(path, args);
		perror("execv");
	}

out:
	php_embed_shutdown();

	thread_destroy();

	return 0;
usage:
	fprintf(stderr, 
		"usage: %s [[-D] [-d <delay>] [-t <threads>] [-r] [ -c <path|file>] [-m | -v | -i] --] <phpfile> args...\n"
		"    -D              Debug info\n"
		"    -d <delay>      Delay seconds\n"
		"    -t <threads>    Max threads\n"
		"    -r              Auto reload\n"
		"    -c <path|file>  Look for php.ini file in this directory\n"
		"    -m              PHP extension list\n"
		"    -v              PHP Version\n"
		"    -i              PHP information\n"
		, argv[0]);
	return 255;
}
