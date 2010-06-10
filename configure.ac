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

dnl ===========================================================================
dnl Feature macro stuff borrowed from Python's configure.in

# The later defininition of _XOPEN_SOURCE disables certain features
# on Linux, so we need _GNU_SOURCE to re-enable them (makedev, tm_zone).
AC_DEFINE(_GNU_SOURCE, 1, [Define on Linux to activate all library features])

# The later defininition of _XOPEN_SOURCE and _POSIX_C_SOURCE disables
# certain features on NetBSD, so we need _NETBSD_SOURCE to re-enable
# them.
AC_DEFINE(_NETBSD_SOURCE, 1, [Define on NetBSD to activate all library features])

# The later defininition of _XOPEN_SOURCE and _POSIX_C_SOURCE disables
# certain features on FreeBSD, so we need __BSD_VISIBLE to re-enable
# them.
AC_DEFINE(__BSD_VISIBLE, 1, [Define on FreeBSD to activate all library features])

# The later defininition of _XOPEN_SOURCE and _POSIX_C_SOURCE disables
# u_int on Irix 5.3. Defining _BSD_TYPES brings it back.
AC_DEFINE(_BSD_TYPES, 1, [Define on Irix to enable u_int])

# The later defininition of _XOPEN_SOURCE and _POSIX_C_SOURCE disables
# certain features on Mac OS X, so we need _DARWIN_C_SOURCE to re-enable
# them.
AC_DEFINE(_DARWIN_C_SOURCE, 1, [Define on Darwin to activate all library features])

define_xopen_source=yes

ac_sys_system=`uname -s`
if test "$ac_sys_system" = "AIX" -o "$ac_sys_system" = "Monterey64" \
   -o "$ac_sys_system" = "UnixWare" -o "$ac_sys_system" = "OpenUNIX"; then
        ac_sys_release=`uname -v`
else
        ac_sys_release=`uname -r`
fi

# Some systems cannot stand _XOPEN_SOURCE being defined at all; they
# disable features if it is defined, without any means to access these
# features as extensions. For these systems, we skip the definition of
# _XOPEN_SOURCE. Before adding a system to the list to gain access to
# some feature, make sure there is no alternative way to access this
# feature. Also, when using wildcards, make sure you have verified the
# need for not defining _XOPEN_SOURCE on all systems matching the
# wildcard, and that the wildcard does not include future systems
# (which may remove their limitations).
dnl quadrigraphs "@<:@" and "@:>@" produce "[" and "]" in the output
case $ac_sys_system/$ac_sys_release in
  # On OpenBSD, select(2) is not available if _XOPEN_SOURCE is defined,
  # even though select is a POSIX function. Reported by J. Ribbens.
  # Reconfirmed for OpenBSD 3.3 by Zachary Hamm, for 3.4 by Jason Ish.
  OpenBSD/2.* | OpenBSD/3.@<:@0123456789@:>@ | OpenBSD/4.@<:@0123@:>@)
    define_xopen_source=no
    # OpenBSD undoes our definition of __BSD_VISIBLE if _XOPEN_SOURCE is
    # also defined. This can be overridden by defining _BSD_SOURCE
    # As this has a different meaning on Linux, only define it on OpenBSD
    AC_DEFINE(_BSD_SOURCE, 1, [Define on OpenBSD to activate all library features])
    ;;
  # Defining _XOPEN_SOURCE on NetBSD version prior to the introduction of
  # _NETBSD_SOURCE disables certain features (eg. setgroups). Reported by
  # Marc Recht
  NetBSD/1.5 | NetBSD/1.5.* | NetBSD/1.6 | NetBSD/1.6.* | NetBSD/1.6@<:@A-S@:>@)
    define_xopen_source=no;;
  # On Solaris 2.6, sys/wait.h is inconsistent in the usage
  # of union __?sigval. Reported by Stuart Bishop.
  SunOS/5.6)
    define_xopen_source=no;;
  # On UnixWare 7, u_long is never defined with _XOPEN_SOURCE,
  # but used in /usr/include/netinet/tcp.h. Reported by Tim Rice.
  # Reconfirmed for 7.1.4 by Martin v. Loewis.
  OpenUNIX/8.0.0| UnixWare/7.1.@<:@0-4@:>@)
    define_xopen_source=no;;
  # On OpenServer 5, u_short is never defined with _XOPEN_SOURCE,
  # but used in struct sockaddr.sa_family. Reported by Tim Rice.
  SCO_SV/3.2)
    define_xopen_source=no;;
  # On FreeBSD 4, the math functions C89 does not cover are never defined
  # with _XOPEN_SOURCE and __BSD_VISIBLE does not re-enable them.
  FreeBSD/4.*)
    define_xopen_source=no;;
  # On MacOS X 10.2, a bug in ncurses.h means that it craps out if
  # _XOPEN_EXTENDED_SOURCE is defined. Apparently, this is fixed in 10.3, which
  # identifies itself as Darwin/7.*
  # On Mac OS X 10.4, defining _POSIX_C_SOURCE or _XOPEN_SOURCE
  # disables platform specific features beyond repair.
  # On Mac OS X 10.3, defining _POSIX_C_SOURCE or _XOPEN_SOURCE
  # has no effect, don't bother defining them
  Darwin/@<:@6789@:>@.*)
    define_xopen_source=no;;
  # On AIX 4 and 5.1, mbstate_t is defined only when _XOPEN_SOURCE == 500 but
  # used in wcsnrtombs() and mbsnrtowcs() even if _XOPEN_SOURCE is not defined
  # or has another value. By not (re)defining it, the defaults come in place.
  AIX/4)
    define_xopen_source=no;;
  AIX/5)
    if test `uname -r` -eq 1; then
      define_xopen_source=no
    fi
    ;;
  # On QNX 6.3.2, defining _XOPEN_SOURCE prevents netdb.h from
  # defining NI_NUMERICHOST.
  QNX/6.3.2)
    define_xopen_source=no
    ;;

esac

if test $define_xopen_source = yes
then
  # On Solaris w/ g++ it appears that _XOPEN_SOURCE has to be
  # defined precisely as g++ defines it
  # Furthermore, on Solaris 10, XPG6 requires the use of a C99
  # compiler
  case $ac_sys_system/$ac_sys_release in
    SunOS/5.8|SunOS/5.9|SunOS/5.10)
      AC_DEFINE(_XOPEN_SOURCE, 500,
                Define to the level of X/Open that your system supports)
      ;;
    *)
      AC_DEFINE(_XOPEN_SOURCE, 600,
                Define to the level of X/Open that your system supports)
      ;;
  esac

  # On Tru64 Unix 4.0F, defining _XOPEN_SOURCE also requires
  # definition of _XOPEN_SOURCE_EXTENDED and _POSIX_C_SOURCE, or else
  # several APIs are not declared. Since this is also needed in some
  # cases for HP-UX, we define it globally.
  # except for Solaris 10, where it must not be defined,
  # as it implies XPG4.2
  case $ac_sys_system/$ac_sys_release in
    SunOS/5.10)
      ;;
    *)
      AC_DEFINE(_XOPEN_SOURCE_EXTENDED, 1,
                Define to activate Unix95-and-earlier features)
      ;;
  esac

  AC_DEFINE(_POSIX_C_SOURCE, 200112L, Define to activate features from IEEE Stds 1003.1-2001)

fi

dnl End of feature macro stuff borrowed from Python's configure.in
dnl ===========================================================================

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

AC_CHECK_FUNCS(asprintf)
AC_CHECK_FUNCS(gethostname)
AC_CHECK_FUNCS(getpwuid)
AC_CHECK_FUNCS(gettimeofday)
AC_CHECK_FUNCS(mkstemp)
AC_CHECK_FUNCS(realpath)
AC_CHECK_FUNCS(snprintf)
AC_CHECK_FUNCS(strndup)
AC_CHECK_FUNCS(utimes)
AC_CHECK_FUNCS(vasprintf)
AC_CHECK_FUNCS(vsnprintf)

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
#include <stdio.h>
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

cat <<EOF >config.h.tmp
#ifndef CCACHE_CONFIG_H
#define CCACHE_CONFIG_H
EOF
cat config.h >>config.h.tmp
echo '#endif' >>config.h.tmp
mv config.h.tmp config.h

version=`(git describe --dirty 2>/dev/null || echo vunknown) | sed -e 's/v//' -e 's/-/+/' -e 's/-/_/g'`
echo "const char CCACHE_VERSION@<:@@:>@ = \"$version\";" >version.c

mkdir -p .deps

if test x$use_bundled_zlib = xyes; then
   AC_MSG_WARN(using bundled zlib)
fi

AC_MSG_NOTICE(now build ccache by running make)
