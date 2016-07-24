dnl
dnl $Id$
dnl

PHP_ARG_ENABLE(cde, whether to enable context-dependent escaping support,
[  --enable-cde         Enable context-dependent escaping support], no)

if test "$PHP_CDE" != "no"; then
  AC_DEFINE(HAVE_CDE, 1, [ ])
  PHP_NEW_EXTENSION(cde, cde.c, $ext_shared)
fi
