/*
   +----------------------------------------------------------------------+
   | PHP Version 7                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2016 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Michael Vostrikov <michael.vostrikov@gmail.com>              |
   +----------------------------------------------------------------------+
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "php_cde.h"

#include "SAPI.h"
#include "ext/standard/info.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "ext/standard/php_string.h"
#include "ext/standard/html.h"

#if HAVE_CDE



/* {{{ arginfo */

ZEND_BEGIN_ARG_INFO_EX(arginfo_PHPEscaper_escape, 0, 0, 1)
ZEND_ARG_INFO(0, string)
ZEND_ARG_INFO(0, context)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_PHPEscaper_registerHandler, 0, 0, 2)
ZEND_ARG_INFO(0, escaper_function)
ZEND_ARG_INFO(0, context)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_PHPEscaper_unregisterHandler, 0, 0, 1)
ZEND_ARG_INFO(0, context)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_PHPEscaper_getHandlers, 0, 0, 0)
ZEND_END_ARG_INFO()

/* }}} */



/* {{{ class and methods */

PHP_METHOD(PHPEscaper, escape);
PHP_METHOD(PHPEscaper, registerHandler);
PHP_METHOD(PHPEscaper, unregisterHandler);
PHP_METHOD(PHPEscaper, getHandlers);


zend_class_entry *php_escaper_ce;

const zend_function_entry php_escaper_functions[] = {
	PHP_ME(PHPEscaper, escape,              arginfo_PHPEscaper_escape,              ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(PHPEscaper, registerHandler,     arginfo_PHPEscaper_registerHandler,     ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(PHPEscaper, unregisterHandler,	arginfo_PHPEscaper_unregisterHandler,	ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(PHPEscaper, getHandlers,         arginfo_PHPEscaper_getHandlers,         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_FE_END
};

/* }}} */



/* {{{ module definition */

static PHP_MINFO_FUNCTION(cde);

static PHP_MINIT_FUNCTION(cde);
static PHP_MSHUTDOWN_FUNCTION(cde);
static PHP_RINIT_FUNCTION(cde);
static PHP_RSHUTDOWN_FUNCTION(cde);


zend_module_entry cde_module_entry = {
	STANDARD_MODULE_HEADER,
	"cde",
	NULL,
	PHP_MINIT(cde),
	PHP_MSHUTDOWN(cde),
	PHP_RINIT(cde),
	PHP_RSHUTDOWN(cde),
	PHP_MINFO(cde),
	PHP_CDE_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_CDE
ZEND_GET_MODULE(cde)
#endif


ZEND_DECLARE_MODULE_GLOBALS(cde);

/* }}} */



PHP_MINFO_FUNCTION(cde)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "context-dependent escaping (PHPEscaper)", "enabled");
	php_info_print_table_end();
}



static inline void php_cde_init_globals(zend_cde_globals *G)
{
	ZEND_TSRMLS_CACHE_UPDATE();
	memset(G, 0, sizeof(*G));
}

static inline void php_cde_shutdown_globals(zend_cde_globals *G)
{
}

PHP_MINIT_FUNCTION(cde)
{
	ZEND_INIT_MODULE_GLOBALS(cde, php_cde_init_globals, php_cde_shutdown_globals);

	zend_class_entry tmp_ce;
	INIT_CLASS_ENTRY(tmp_ce, "PHPEscaper", php_escaper_functions);
	php_escaper_ce = zend_register_internal_class(&tmp_ce TSRMLS_CC);

	return SUCCESS;
}


PHP_MSHUTDOWN_FUNCTION(cde)
{

#ifdef ZTS
	ts_free_id(cde_globals_id);
#else
	// do nothing
#endif

	return SUCCESS;
}


/* similar to spl autoload */

typedef struct {
	zend_function *func_ptr;
	zval obj;
	zval closure;
	zend_class_entry *ce;
} escaper_func_info;


static void escaper_func_info_dtor(zval *element)
{
	escaper_func_info *efi = (escaper_func_info*)Z_PTR_P(element);
	if (!Z_ISUNDEF(efi->obj)) {
		zval_ptr_dtor(&efi->obj);
	}
	if (!Z_ISUNDEF(efi->closure)) {
		zval_ptr_dtor(&efi->closure);
	}
	efree(efi);
}

PHP_RINIT_FUNCTION(cde)
{
	ALLOC_HASHTABLE(CDE_G(escaper_functions));
	zend_hash_init(CDE_G(escaper_functions), 1, NULL, escaper_func_info_dtor, 0);

	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(cde)
{
	zend_hash_destroy(CDE_G(escaper_functions));
	FREE_HASHTABLE(CDE_G(escaper_functions));
	CDE_G(escaper_functions) = NULL;

	return SUCCESS;
}




/* {{{ proto bool PHPEscaper::registerHandler(string context, mixed escaper_function)
   Register given function as context-dependent escaping handler */
PHP_METHOD(PHPEscaper, registerHandler)
{
	zval *escaper_function = NULL;
	zend_string *context;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "Sz", &context, &escaper_function) == FAILURE) {
		return;
	}


	zend_string *hash_key = context;

	escaper_func_info efi;
	zend_string *func_name;
	zend_fcall_info_cache fcc;
	char *error = NULL;
	zend_object *obj_ptr;

	zend_bool has_errors = FALSE;

	if (!zend_is_callable_ex(escaper_function, NULL, IS_CALLABLE_STRICT, &func_name, &fcc, &error)) {
		efi.ce = fcc.calling_scope;
		efi.func_ptr = fcc.function_handler;
		obj_ptr = fcc.object;

		if (Z_TYPE_P(escaper_function) == IS_ARRAY) {
			if (!obj_ptr && efi.func_ptr && !(efi.func_ptr->common.fn_flags & ZEND_ACC_STATIC)) {
				zend_throw_exception_ex(NULL, 0, "Passed array specifies a non static method but no object (%s)", error);
			} else {
				zend_throw_exception_ex(NULL, 0, "Passed array does not specify %s %smethod (%s)", efi.func_ptr ? "a callable" : "an existing", !obj_ptr ? "static " : "", error);
			}
		} else if (Z_TYPE_P(escaper_function) == IS_STRING) {
			zend_throw_exception_ex(NULL, 0, "Function '%s' not %s (%s)", ZSTR_VAL(func_name), efi.func_ptr ? "callable" : "found", error);
		} else {
			zend_throw_exception_ex(NULL, 0, "Illegal value passed (%s)", error);
		}

		has_errors = TRUE;
	} else if (fcc.function_handler->type == ZEND_INTERNAL_FUNCTION &&
		fcc.function_handler->internal_function.handler == ZEND_MN(PHPEscaper_escape)) {
		zend_throw_exception_ex(NULL, 0, "Function context_dependent_escaper_call() cannot be registered");

		has_errors = TRUE;
	}

	if (has_errors) {
		if (error) {
			efree(error);
		}
		zend_string_release(func_name);
		RETURN_FALSE;
	}



	if (error) {
		efree(error);
	}


	efi.ce = fcc.calling_scope;
	efi.func_ptr = fcc.function_handler;
	obj_ptr = fcc.object;

	if (Z_TYPE_P(escaper_function) == IS_OBJECT) {
		ZVAL_COPY(&efi.closure, escaper_function);
	} else {
		ZVAL_UNDEF(&efi.closure);
	}
	zend_string_release(func_name);


	if (CDE_G(escaper_functions) && zend_hash_exists(CDE_G(escaper_functions), hash_key)) {
		if (!Z_ISUNDEF(efi.closure)) {
			Z_DELREF_P(&efi.closure);
		}

		RETURN_FALSE;
	}


	if (obj_ptr && !(efi.func_ptr->common.fn_flags & ZEND_ACC_STATIC)) {
		ZVAL_OBJ(&efi.obj, obj_ptr);
		Z_ADDREF(efi.obj);
	} else {
		ZVAL_UNDEF(&efi.obj);
	}

	void* new_entry = zend_hash_add_mem(CDE_G(escaper_functions), hash_key, &efi, sizeof(escaper_func_info));
	if (new_entry == NULL) {
		if (obj_ptr && !(efi.func_ptr->common.fn_flags & ZEND_ACC_STATIC)) {
			Z_DELREF(efi.obj);
		}
		if (!Z_ISUNDEF(efi.closure)) {
			Z_DELREF(efi.closure);
		}
	}


	RETURN_TRUE;
}
/* }}} */



/* {{{ proto bool PHPEscaper::unregisterHandler(string context)
   Unregisters escaping handler for given context */
PHP_METHOD(PHPEscaper, unregisterHandler)
{
	zend_string *context = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "S", &context) == FAILURE) {
		return;
	}

	/* remove handler for given context */
	int del_result;
	del_result = zend_hash_del(CDE_G(escaper_functions), context);

	RETURN_BOOL(del_result == SUCCESS);
}
/* }}} */



/* {{{ proto array PHPEscaper::getHandlers()
   Return all registered functions */
PHP_METHOD(PHPEscaper, getHandlers)
{
	if (zend_parse_parameters_none() == FAILURE) {
		return;
	}


	escaper_func_info *efi;
	zend_string *key;
	array_init(return_value);

	ZEND_HASH_FOREACH_STR_KEY_PTR(CDE_G(escaper_functions), key, efi) {
		if (!Z_ISUNDEF(efi->closure)) {
			Z_ADDREF(efi->closure);
			add_assoc_zval(return_value, key->val, &efi->closure);
		} else if (efi->func_ptr->common.scope) {
			zval tmp;

			array_init(&tmp);
			if (!Z_ISUNDEF(efi->obj)) {
				Z_ADDREF(efi->obj);
				add_next_index_zval(&tmp, &efi->obj);
			} else {
				add_next_index_str(&tmp, zend_string_copy(efi->ce->name));
			}
			add_next_index_str(&tmp, zend_string_copy(efi->func_ptr->common.function_name));
			add_assoc_zval(return_value, key->val, &tmp);
		} else {
			if (strncmp(ZSTR_VAL(efi->func_ptr->common.function_name), "__lambda_func", sizeof("__lambda_func") - 1)) {
				add_assoc_str(return_value, key->val, zend_string_copy(efi->func_ptr->common.function_name));
			} else {
				// TODO: object class name
				add_assoc_str(return_value, key->val, zend_string_copy(efi->func_ptr->common.function_name));
			}
		}
	} ZEND_HASH_FOREACH_END();
}
/* }}} */



zend_bool call_escaper(zval* str, const char* context_name, int context_name_len, zval* retval)
{
	escaper_func_info* efi = (escaper_func_info*)zend_hash_str_find_ptr(
		CDE_G(escaper_functions),
		context_name, context_name_len
	);

	if (efi) {
		zend_call_method_with_1_params(
			(Z_ISUNDEF(efi->obj) ? NULL : &efi->obj),
			efi->ce, &efi->func_ptr,
			"context_dependent_escaper_call",
			retval, str
		);
		return TRUE;
	} else {
		if (!strncmp(context_name, "html", MAX(context_name_len, sizeof("html") - 1))) {

			// htmlspecialchars is default handler for html context

			zval flags;
			ZVAL_LONG(&flags, ENT_QUOTES | ENT_SUBSTITUTE);

			zend_call_method_with_2_params(NULL, NULL, NULL,
				"htmlspecialchars", retval, str, &flags
			);
			return TRUE;
		}

		return FALSE;
	}
}


/* {{{ proto bool PHPEscaper::escape(string string[, string context])
   Calls escaping handler for given context with given string
   Tries to find escaping handler in registered handlers
   String and context can be real string or object with __toString method
   If no context given, default context is 'html'
   Throws exception if handler not found */
PHP_METHOD(PHPEscaper, escape)
{
	zval *str;
	zend_string *context = NULL;

#ifndef FAST_ZPP
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z|S", &str, &context) == FAILURE) {
		return;
	}
#else
	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_ZVAL(str)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR(context)
		ZEND_PARSE_PARAMETERS_END();
#endif

	zend_bool call_escaper_result;

	zval current_str;
	ZVAL_DUP(&current_str, str);

	// html is default context
	if (!context) {
		const char* context_name = "html";
		int context_name_len = strlen(context_name);

		call_escaper_result = call_escaper(
			&current_str,
			context_name, context_name_len,
			&current_str
		);

		if (!call_escaper_result) {
			zend_throw_exception_ex(NULL, 0, "Unknown context '%s'", context_name);
		}


		if (!call_escaper_result) {
			RETVAL_NULL();
		} else {
			RETVAL_ZVAL(&current_str, 1, 1);
		}

		return;
	}



	zval context_list;
	array_init(&context_list);

	const char* p_delimiter = "|";
	zend_string* delimiter = zend_string_init(p_delimiter, strlen(p_delimiter), 1);

	php_explode(delimiter, context, &context_list, ZEND_LONG_MAX);
	zend_string_release(delimiter);


	zval *context_entry;
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL(context_list), context_entry) {
		zend_string* context_entry_trimmed = php_trim(context_entry->value.str, NULL, 0, 3);

		call_escaper_result = call_escaper(
			&current_str,
			context_entry_trimmed->val, context_entry_trimmed->len,
			&current_str
		);

		if (!call_escaper_result) {
			zend_throw_exception_ex(NULL, 0, "Unknown context '%s'", context_entry_trimmed->val);
			zend_string_release(context_entry_trimmed);
			break;
		}

		zend_string_release(context_entry_trimmed);
	} ZEND_HASH_FOREACH_END();

	if (!call_escaper_result) {
		RETVAL_NULL();
	} else {
		RETVAL_ZVAL(&current_str, 1, 1);
	}

	zval_ptr_dtor(&context_list);
}
/* }}} */

#endif	/* HAVE_CDE */

/*
* Local variables:
* tab-width: 4
* c-basic-offset: 4
* End:
* vim600: sw=4 ts=4 fdm=marker
* vim<600: sw=4 ts=4
*/
