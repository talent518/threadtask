#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <getopt.h>

#include <zend.h>
#include <zend_extensions.h>
#include <zend_builtin_functions.h>
#include <zend_exceptions.h>
#include <embed/php_embed.h>
#include <standard/php_var.h>
#include <standard/info.h>

#include "func.h"
#include "hash.h"

static volatile int is_perf = 0;

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

	/* do not close stdout and stderr */
	s_in->flags |= PHP_STREAM_FLAG_NO_CLOSE;
	s_out->flags |= PHP_STREAM_FLAG_NO_CLOSE;
	s_err->flags |= PHP_STREAM_FLAG_NO_CLOSE;

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
	if(is_perf) {
		char *ret = NULL;
		if(asprintf(&ret, "%sopcache.jit=0\n", sapi_module->ini_entries) > 0) {
			free(sapi_module->ini_entries);
			sapi_module->ini_entries = ret;
		}
	}
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
#if PHP_VERSION_ID >= 80000
	zend_hash_sort(&sorted_registry, module_name_cmp, 0);
#else
	zend_hash_sort(&sorted_registry, (compare_func_t) module_name_cmp, 0);
#endif
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

// ======================= begin perf =======================

static void (*_zend_execute_ex) (zend_execute_data *execute_data);
static void (*_zend_execute_internal)(zend_execute_data *execute_data, zval *return_value);

static char *perf_basename(char *file) {
	char buff[1024], *pos;
	zval *name = zend_get_constant_str(ZEND_STRL("ROOT"));
	
	if(name) {
		if(strncmp(file, Z_STRVAL_P(name), Z_STRLEN_P(name)) || file[Z_STRLEN_P(name)] != '/') {
			goto retry;
		} else {
			return file + Z_STRLEN_P(name) + 1;
		}
	} else if(VCWD_GETCWD(buff, sizeof(buff))) {
		int size = strlen(buff);
		if(strncmp(file, buff, size) || file[size] != '/') {
			goto retry;
		} else {
			return file + size + 1;
		}
	} else {
	retry:
		pos = strrchr(file, '/');
		if(pos) {
			return pos + 1;
		} else {
			return file;
		}
	}
}

static char *perf_get_function_name(zend_execute_data *call) {
	zend_function *func;
	zend_string *name;
	char *ret = NULL;

	func = call->func;

	if (func && func->common.function_name) {
		if (Z_TYPE(call->This) == IS_OBJECT) {
			zend_object *object = Z_OBJ(call->This);
			/* $this may be passed into regular internal functions */
			if (func->common.scope) {
				name = func->common.scope->name;
		#if PHP_VERSION_ID >= 70300
			} else if (object->handlers->get_class_name == zend_std_get_class_name) {
		#else
			} else if (object->handlers->get_class_name == std_object_handlers.get_class_name) {
		#endif
				name = object->ce->name;
			} else {
				name = object->handlers->get_class_name(object);
			}

			if(func->op_array.fn_flags & ZEND_ACC_CLOSURE) {
				zend_spprintf(&ret, 1024, "%s->{closure}():%d", ZSTR_VAL(name), call->opline->lineno);
			} else {
				zend_spprintf(&ret, 1024, "%s->%s()", ZSTR_VAL(name), ZSTR_VAL(func->common.function_name));
			}
		} else if (func->common.scope) {
			if(func->op_array.fn_flags & ZEND_ACC_CLOSURE) {
				zend_spprintf(&ret, 1024, "%s::{closure}():%d", ZSTR_VAL(func->common.scope->name), call->opline->lineno);
			} else {
				zend_spprintf(&ret, 1024, "%s::%s()", ZSTR_VAL(func->common.scope->name), ZSTR_VAL(func->common.function_name));
			}
		} else if(func->op_array.fn_flags & ZEND_ACC_CLOSURE) {
			zend_spprintf(&ret, 1024, "{closure}():%s:%d", perf_basename(ZSTR_VAL(func->op_array.filename)), call->opline->lineno);
		} else {
			zend_spprintf(&ret, 1024, "%s()", ZSTR_VAL(func->common.function_name));
		}
	} else {
		uint32_t include_kind = 0;
	retry:
		if (func && ZEND_USER_CODE(func->common.type) && call->opline->opcode == ZEND_INCLUDE_OR_EVAL) {
			include_kind = call->opline->extended_value;
		}

		switch (include_kind) {
			case ZEND_EVAL:
				zend_spprintf(&ret, 1024, "eval:%s:%d", perf_basename(ZSTR_VAL(func->op_array.filename)), call->opline->lineno);
				return ret;
			case ZEND_INCLUDE:
				name = ZSTR_KNOWN(ZEND_STR_INCLUDE);
				break;
			case ZEND_REQUIRE:
				name = ZSTR_KNOWN(ZEND_STR_REQUIRE);
				break;
			case ZEND_INCLUDE_ONCE:
				name = ZSTR_KNOWN(ZEND_STR_INCLUDE_ONCE);
				break;
			case ZEND_REQUIRE_ONCE:
				name = ZSTR_KNOWN(ZEND_STR_REQUIRE_ONCE);
				break;
			default:
				call = call->prev_execute_data;
				if(call && call->func) {
					func = call->func;
					include_kind = 0;
					goto retry;
				} else {
					return NULL;
				}
		}

		zend_spprintf(&ret, 1024, "%s:%s:%d", ZSTR_VAL(name), perf_basename(ZSTR_VAL(func->op_array.filename)), call->opline->lineno);
	}

	return ret;
}

static ts_hash_table_t perf_ht, perf_ht_internal;
typedef struct {
	int n;
	float m;
	double t;
} perf_t;

static void perf_free(value_t *v) {
	
}

static void perf_record(ts_hash_table_t *perf_ht, char *func, double t) {
	int size = strlen(func);
	zend_long h = zend_get_hash_value(func, size);
	perf_t v;

	ts_hash_table_wr_lock(perf_ht);
	{
		if(hash_table_quick_find(&perf_ht->ht, func, size, h, (value_t*) &v) == FAILURE) {
			memset(&v, 0, sizeof(v));
		}

		v.n ++;
		v.t += t;
		if(t > v.m) {
			v.m = t;
		}

		hash_table_quick_update(&perf_ht->ht, func, size, h, (value_t*) &v, NULL);
	}
	ts_hash_table_wr_unlock(perf_ht);
}

static void perf_execute_ex(zend_execute_data *execute_data) {
	char *func = perf_get_function_name(execute_data);

	if(func) {
		double t = microtime();

		_zend_execute_ex(execute_data);
		perf_record(&perf_ht, func, microtime() - t);

		efree(func);
	} else {
		_zend_execute_ex(execute_data);
	}
}

static void perf_execute_internal(zend_execute_data *execute_data, zval *return_value) {
	char *func = perf_get_function_name(execute_data);

	if(func) {
		double t = microtime();

		if (EXPECTED(_zend_execute_internal == NULL)) {
			/* saves one function call if zend_execute_internal is not used */
			execute_data->func->internal_function.handler(execute_data, return_value);
		} else {
			_zend_execute_internal(execute_data, return_value);
		}

		perf_record(&perf_ht_internal, func, microtime() - t);
		efree(func);
	} else {
		if (EXPECTED(_zend_execute_internal == NULL)) {
			/* saves one function call if zend_execute_internal is not used */
			execute_data->func->internal_function.handler(execute_data, return_value);
		} else {
			_zend_execute_internal(execute_data, return_value);
		}
	}
}

static int perf_sort_func(const bucket_t *a, const bucket_t *b) {
	perf_t *ap = (perf_t*) &a->value;
	perf_t *bp = (perf_t*) &b->value;

	if(ap->t > bp->t) {
		return 1;
	} else if(ap->t < bp->t) {
		return -1;
	} else {
		return 0;
	}
}

int compare_key_nature(const bucket_t *a, const bucket_t *b) {
    if(a->nKeyLength == 0) {
        if(b->nKeyLength == 0) {
            if(a->h > b->h) {
                return 1;
            } else if(a->h < b->h) {
                return -1;
            } else {
                return 0;
            }
        } else {
            return -1;
        }
    } else if(b->nKeyLength == 0) {
        return 1;
    } else {
        return strnatcmp(a->arKey, a->nKeyLength, b->arKey, b->nKeyLength, 0);
    }
}

static int perf_apply_avg_func(bucket_t *pDest) {
	perf_t *p = (perf_t*) &pDest->value;

	p->t = p->t / (double) p->n;

	return HASH_TABLE_APPLY_KEEP;
}
static int perf_apply_print_func(bucket_t *pDest, int *i) {
	perf_t *p = (perf_t*) &pDest->value;

	fprintf(stderr, "%04d %10d %10.6lf %10.6lf %s\n", *i, p->n, p->t, p->m, pDest->arKey);

	(*i) ++;

	return HASH_TABLE_APPLY_REMOVE;
}

// ======================= end perf =======================

static const char *options = "pkIDd:t:rc:mivh";
static struct option OPTIONS[] = {
    {"perf",           0, 0, 'p' },
    {"key",            0, 0, 'k' },
    {"internal",       0, 0, 'I' },
    {"debug",          0, 0, 'D' },
    {"delay",          1, 0, 'd' },
    {"threads",        1, 0, 't' },
    {"reload",         0, 0, 'r' },
    {"config",         1, 0, 'c' },
    {"modules",        0, 0, 'm' },
    {"info",           0, 0, 'i' },
    {"version",        0, 0, 'v' },
    {"help",           0, 0, 'h' },

    {NULL,              0, 0, 0 }
};

int main(int argc, char *argv[]) {
	zend_file_handle file_handle;
	int opt, ind = 0;
	char path[PATH_MAX];
	size_t sz = readlink("/proc/self/exe", path, PATH_MAX);
	path[sz] = '\0';
	char *ini_path_override = NULL;
	int is_module_list = 0, is_print_info = 0, is_key = 0, is_internal = 0;

	while((opt = getopt_long(argc, argv, options, OPTIONS, &ind)) != -1) {
		switch(opt) {
			case 'p':
				is_perf = 1;
				break;
			case 'k':
				is_key = 1;
				break;
			case 'I':
				is_internal = 1;
				break;
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

	if(is_perf) {
		_zend_execute_ex = zend_execute_ex;
		zend_execute_ex  = perf_execute_ex;
		_zend_execute_internal = zend_execute_internal;
		if(is_internal) {
			zend_execute_internal = perf_execute_internal;
			ts_hash_table_init_ex(&perf_ht_internal, 128, perf_free);
		}

		// printf("value_t: %ld, perf_t: %ld\n", sizeof(value_t), sizeof(perf_t));
		ts_hash_table_init_ex(&perf_ht, 128, perf_free);
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

	if(is_perf) {
		int i = 1;
		fprintf(stderr, "========================== PERF USER CODE =========================\n");
		fprintf(stderr, "  ID      Times        AVG        MAX INFO\n");
		fprintf(stderr, "-------------------------------------------------------------------\n");
		hash_table_apply(&perf_ht.ht, perf_apply_avg_func);
		hash_table_sort(&perf_ht.ht, is_key ? compare_key_nature : perf_sort_func, 0);
		hash_table_apply_with_argument(&perf_ht.ht, (hash_apply_func_arg_t) perf_apply_print_func, &i);
		ts_hash_table_destroy_ex(&perf_ht, 0);
		if(is_internal) {
			i = 1;
			fprintf(stderr, "========================== PERF INTERNAL ==========================\n");
			fprintf(stderr, "  ID      Times        AVG        MAX INFO\n");
			fprintf(stderr, "-------------------------------------------------------------------\n");
			hash_table_apply(&perf_ht_internal.ht, perf_apply_avg_func);
			hash_table_sort(&perf_ht_internal.ht, is_key ? compare_key_nature : perf_sort_func, 0);
			hash_table_apply_with_argument(&perf_ht_internal.ht, (hash_apply_func_arg_t) perf_apply_print_func, &i);
			ts_hash_table_destroy_ex(&perf_ht_internal, 0);
		}
		zend_execute_ex = _zend_execute_ex;
		zend_execute_internal = _zend_execute_internal;
	}

	if(isReload) {
		dprintf("RELOAD THREADTASK\n");
		char **args = (char**) malloc(sizeof(char*)*(argc+1));
		memcpy(args, argv, sizeof(char*)*argc);
		args[argc] = NULL;
		execv(path, args);
		perror("execv");
	}
	
	{
		zval func, retval, params[1];

		ZVAL_LONG(&params[0], SIGINT);

		ZVAL_STRING(&func, "task_wait");

		call_user_function(NULL, NULL, &func, &retval, 1, params);
		
		zval_ptr_dtor(&func);
		zval_ptr_dtor(&retval);
	}

out:
	php_embed_shutdown();
	
	thread_destroy();

	return 0;
usage:
	fprintf(stderr, 
		"usage: %s [options] <phpfile> args...\n"
		"    -h,--help               This help text\n"
		"    -p,--perf               Perf info\n"
		"    -k,--key                Perf sort for key\n"
		"    -I,--internal           Perf info for internal\n"
		"    -D,--debug              Debug info\n"
		"    -d,--delay <delay>      Delay seconds\n"
		"    -t,--threads <threads>  Max threads\n"
		"    -r,--reload             Auto reload\n"
		"    -c,--config <path|file> Look for php.ini file in this directory\n"
		"    -m,--modules            PHP extension list\n"
		"    -v,--version            PHP Version\n"
		"    -i,--info               PHP information\n"
		, argv[0]);
	return 255;
}
