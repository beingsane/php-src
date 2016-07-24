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

ZEND_BEGIN_ARG_INFO_EX(arginfo_escape_handler_call, 0, 0, 1)
ZEND_ARG_INFO(0, string)
ZEND_ARG_INFO(0, context)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_set_escape_handler, 0, 0, 1)
ZEND_ARG_INFO(0, escaper_function)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_restore_escape_handler, 0, 0, 0)
ZEND_END_ARG_INFO()

/* }}} */



/* {{{ functions */

static PHP_FUNCTION(escape_handler_call);
static PHP_FUNCTION(set_escape_handler);
static PHP_FUNCTION(restore_escape_handler);

static const zend_function_entry cde_functions[] = {
	PHP_FE(escape_handler_call,     arginfo_escape_handler_call)
	PHP_FE(set_escape_handler,      arginfo_set_escape_handler)
	PHP_FE(restore_escape_handler,  arginfo_restore_escape_handler)
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
	cde_functions,
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
	php_info_print_table_row(2, "context-dependent escaping", "enabled");
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


PHP_RINIT_FUNCTION(cde)
{
	ZVAL_UNDEF(&CDE_G(current_user_escape_handler));
	zend_stack_init(&CDE_G(user_escape_handlers), sizeof(zval));

	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(cde)
{
	if (Z_TYPE(CDE_G(current_user_escape_handler)) != IS_UNDEF) {
		zval *zeh;
		zeh = &CDE_G(current_user_escape_handler);
		zval_ptr_dtor(zeh);
		ZVAL_UNDEF(&CDE_G(current_user_escape_handler));
	}

	zend_stack_clean(&CDE_G(user_escape_handlers), (void(*)(void *))ZVAL_PTR_DTOR, 1);

	zend_stack_destroy(&CDE_G(user_escape_handlers));

	return SUCCESS;
}


// similar to set_error_handler()

/* {{{ proto string set_escape_handler(callable escape_handler)
Sets a user-defined handler function for context-dependent escaping.  Returns the previously defined escape handler, or false on error */
PHP_FUNCTION(set_escape_handler)
{
	zval *escape_handler;
	zend_string *escape_handler_name = NULL;
	zend_long escape_type = E_ALL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &escape_handler) == FAILURE) {
		return;
	}

	if (Z_TYPE_P(escape_handler) != IS_NULL) { /* NULL == unset */
		if (!zend_is_callable(escape_handler, 0, &escape_handler_name)) {
			zend_error(E_WARNING, "%s() expects the argument (%s) to be a valid callback",
				get_active_function_name(), escape_handler_name ? ZSTR_VAL(escape_handler_name) : "unknown");
			zend_string_release(escape_handler_name);
			return;
		}
		zend_string_release(escape_handler_name);
	}

	if (Z_TYPE(CDE_G(current_user_escape_handler)) != IS_UNDEF) {
		ZVAL_COPY(return_value, &CDE_G(current_user_escape_handler));

		zend_stack_push(&CDE_G(user_escape_handlers), &CDE_G(current_user_escape_handler));
	}

	if (Z_TYPE_P(escape_handler) == IS_NULL) { /* unset user-defined handler */
		ZVAL_UNDEF(&CDE_G(current_user_escape_handler));
		return;
	}

	ZVAL_COPY(&CDE_G(current_user_escape_handler), escape_handler);
}
/* }}} */


/* {{{ proto bool restore_escape_handler(void)
Restores the previously defined escape handler function */
ZEND_FUNCTION(restore_escape_handler)
{
	if (zend_parse_parameters_none() == FAILURE) {
		return;
	}

	if (Z_TYPE(CDE_G(current_user_escape_handler)) != IS_UNDEF) {
		zval tmp_handler;

		ZVAL_COPY_VALUE(&tmp_handler, &CDE_G(current_user_escape_handler));
		ZVAL_UNDEF(&CDE_G(current_user_escape_handler));
		zval_ptr_dtor(&tmp_handler);
	}

	if (zend_stack_is_empty(&CDE_G(user_escape_handlers))) {
		ZVAL_UNDEF(&CDE_G(current_user_escape_handler));
	} else {
		zval *tmp;

		tmp = zend_stack_top(&CDE_G(user_escape_handlers));
		ZVAL_COPY_VALUE(&CDE_G(current_user_escape_handler), tmp);
		zend_stack_del_top(&CDE_G(user_escape_handlers));
	}
	RETURN_TRUE;
}
/* }}} */


/* {{{ proto string escape_handler_call(string string[, string context])
Passes given context and given string to user escape handler
Throws exception if no handler is defined, or call of handler fails */
ZEND_FUNCTION(escape_handler_call)
{
	zval *str;
	zval *context = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z|z", &str, &context) == FAILURE) {
		return;
	}


	if (Z_TYPE(CDE_G(current_user_escape_handler)) == IS_UNDEF) {
		zend_throw_exception_ex(NULL, 0, "Escape handler is not set. Use set_escape_handler(callable) before using escaping operator.");
		RETURN_NULL();
	}


	int params_count = 1;
	zval params[2];
	ZVAL_COPY_VALUE(&params[0], str);
	if (context != NULL) {
		ZVAL_COPY_VALUE(&params[1], context);
		params_count = 2;
	}

	zval* p_handler = &CDE_G(current_user_escape_handler);

	int call_escaper_result = call_user_function(CG(function_table), NULL, p_handler, return_value, params_count, params);
	if (call_escaper_result != SUCCESS) {
		zend_string *str = zval_get_string(return_value);
		zend_throw_exception_ex(NULL, 0, "Unable to call escape handler: ", str->val);
		zend_string_release(str);
		RETURN_NULL();
	}

	// RETVAL_ZVAL(return_value, 0, 0);
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
