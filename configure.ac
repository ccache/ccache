dnl Process this file with autoconf to produce a configure script.

AC_INIT()
AC_PREREQ(2.52)

AC_MSG_NOTICE([Configuring ccache])

AC_CONFIG_HEADER(config.h)

AC_SUBST(extra_sources)
AC_SUBST(include_dev_mk)

AC_ARG_ENABLE(
    dev,
    AS_HELP_STRING(--enable-dev, enable developer mode (requires GNU make)),
    [include_dev_mk='include dev.mk'])

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL

AC_DEFINE([_GNU_SOURCE], 1,
          [Define _GNU_SOURCE so that we get all necessary prototypes])

# The definition of _GNU_SOURCE potentially causes a change of the value
# of _XOPEN_SOURCE. So define it only conditionally.
AH_VERBATIM([_XOPEN_SOURCE],
[/* Define on UNIX to activate XPG/5 features.  */
#if !defined(_XOPEN_SOURCE)
# define _XOPEN_SOURCE
#endif])

# If GCC, turn on warnings.
if test "x$GCC" = "xyes"; then
    CFLAGS="$CFLAGS -Wall -W"
else
    CFLAGS="$CFLAGS -O"
fi

AC_HEADER_DIRENT
AC_HEADER_TIME
AC_HEADER_SYS_WAIT

AC_CHECK_HEADERS(ctype.h pwd.h stdlib.h string.h strings.h sys/time.h)

AC_CHECK_FUNCS(asprintf gethostname getpwuid mkstemp realpath snprintf strndup)
AC_CHECK_FUNCS(utimes vasprintf vsnprintf)

AC_CACHE_CHECK([for compar_fn_t in stdlib.h],ccache_cv_COMPAR_FN_T, [
    AC_TRY_COMPILE(
        [#include <stdlib.h>],
        [void test_fn(void) { qsort(NULL, 0, 0, (__compar_fn_t)NULL); }],
        ccache_cv_COMPAR_FN_T=yes,
        ccache_cv_COMPAR_FN_T=no)])
if test x"$ccache_cv_COMPAR_FN_T" = x"yes"; then
   AC_DEFINE(HAVE_COMPAR_FN_T, 1,
             Define to 1 if you have the `__compar_fn_t' typedef.)
fi

AC_CACHE_CHECK([for C99 vsnprintf],ccache_cv_HAVE_C99_VSNPRINTF,[
AC_TRY_RUN([
#include <sys/types.h>
#include <stdarg.h>
void foo(const char *format, ...) {
    va_list ap;
    int len;
    char buf[5];

    va_start(ap, format);
    len = vsnprintf(0, 0, format, ap);
    va_end(ap);
    if (len != 5) exit(1);

    if (snprintf(buf, 3, "hello") != 5 || strcmp(buf, "he") != 0) exit(1);

    exit(0);
}
main() { foo("hello"); }
],
ccache_cv_HAVE_C99_VSNPRINTF=yes,
ccache_cv_HAVE_C99_VSNPRINTF=no,
ccache_cv_HAVE_C99_VSNPRINTF=cross)])
if test x"$ccache_cv_HAVE_C99_VSNPRINTF" = x"yes"; then
    AC_DEFINE(HAVE_C99_VSNPRINTF, 1,
              Define to 1 if you have the `vsnprintf' function.)
fi

dnl Check for zlib.
AC_CACHE_CHECK(
    [for zlib >= 1.2.3],
    [ccache_cv_zlib_1_2_3],
    AC_TRY_COMPILE(
        [#include <zlib.h>],
        [
        #if (ZLIB_VERNUM >= 0x1230)
        #else
        #error "ZLIB_VERNUM < 0x1230"
        #endif
        ],
        [ccache_cv_zlib_1_2_3=yes],
        [ccache_cv_zlib_1_2_3=no]))
AC_CHECK_LIB(z, gzdopen, true)
if test $ccache_cv_zlib_1_2_3 = yes && test $ac_cv_lib_z_gzdopen = yes; then
    use_bundled_zlib=no
    LIBS="-lz $LIBS"
else
    use_bundled_zlib=yes
    extra_sources="$extra_sources zlib/adler32.c zlib/compress.c zlib/crc32.c"
    extra_sources="$extra_sources zlib/deflate.c zlib/gzio.c zlib/inffast.c"
    extra_sources="$extra_sources zlib/inflate.c zlib/inftrees.c zlib/trees.c"
    extra_sources="$extra_sources zlib/zutil.c"
    CPPFLAGS="$CPPFLAGS -I\$(srcdir)/zlib"
    mkdir -p zlib
fi

AC_C_INLINE

dnl Check for "extern inline".
AC_CACHE_CHECK(
    for extern inline,
    ac_cv_c_extern_inline,
    [
    ac_cv_c_extern_inline=no
    AC_TRY_COMPILE(
        [
        extern $ac_cv_c_inline double foo(double x);
        extern $ac_cv_c_inline double foo(double x) { return x+1.0; };
        double foo (double x) { return x + 1.0; };
        ],
        [foo(1.0)],
        [ac_cv_c_extern_inline="yes"])])
if test "$ac_cv_c_extern_inline" != no ; then
    AC_DEFINE(HAVE_EXTERN_INLINE, 1,
              Define to 1 if your compiler supports extern inline)
fi

AC_CONFIG_FILES([Makefile dev.mk])
AC_OUTPUT

mkdir -p .deps

if test x$use_bundled_zlib = xyes; then
   AC_MSG_WARN(using bundled zlib)
fi

AC_MSG_NOTICE(now build ccache by running make)
