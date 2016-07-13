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

#ifndef PHP_CDE_H
#define PHP_CDE_H

#include "php_version.h"
#define PHP_CDE_VERSION PHP_VERSION

#if HAVE_CDE

extern zend_module_entry cde_module_entry;
#define phpext_cde_ptr &cde_module_entry


ZEND_BEGIN_MODULE_GLOBALS(cde)
	HashTable *escaper_functions;
ZEND_END_MODULE_GLOBALS(cde)

ZEND_EXTERN_MODULE_GLOBALS(cde)


#ifdef ZTS
# define CDE_G(v) ZEND_TSRMG(cde_globals_id, zend_cde_globals *, v)
#else
# define CDE_G(v) (cde_globals.v)
#endif


#else

#define phpext_cde_ptr NULL

#endif

#endif	/* PHP_CDE_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
