dnl Process this file with autoconf to produce a configure script.

AC_INIT()
AC_PREREQ(2.52)

AC_MSG_NOTICE([configuring ccache])

dnl Enable developer mode if dev.mk.in exists.
if test ! -f $srcdir/dev_mode_disabled; then
    dev_mode=yes
    AC_MSG_NOTICE(developer mode enabled)
    AC_CONFIG_FILES([dev.mk])
    include_dev_mk='include dev.mk'
    version=`(git --git-dir=$srcdir/.git describe --dirty 2>/dev/null || echo vunknown) | sed -e 's/v//' -e 's/-/+/' -e 's/-/_/g'`
    mkdir -p src
    echo "extern const char CCACHE_VERSION@<:@@:>@; const char CCACHE_VERSION@<:@@:>@ = \"$version\";" >src/version.cpp
else
    dev_mode=no
    AC_MSG_NOTICE(developer mode disabled)
fi

AC_CONFIG_HEADER(config.h)

AC_CANONICAL_HOST

case $host in
    *mingw32* | *mingw64* | *cygwin* | *wince* | *mingwce*)
        windows_os=yes
        AC_DEFINE(_WIN32_WINNT,0x0600, Windows Vista or newer is required)
        ;;
    *os400* | *aix*)
        AR="ar -X64"
        ;;
esac

AC_SUBST(disable_cross)
AC_SUBST(disable_man)
AC_SUBST(extra_libs)
AC_SUBST(extra_sources)
AC_SUBST(extra_warnings)
AC_SUBST(include_dev_mk)

m4_include(m4/feature_macros.m4)
m4_include(m4/ax_cxx_compile_stdcxx.m4)
m4_include(m4/ax_check_compile_flag.m4)
m4_include(m4/ax_pthread.m4)
m4_include(m4/clang.m4)

mkdir -p .deps src/third_party unittest

dnl Checks for programs.
AC_PROG_CC
AX_CXX_COMPILE_STDCXX(11, noext, mandatory)
_AC_LANG_COMPILER_CLANG
AC_PROG_INSTALL

AX_CHECK_COMPILE_FLAG([-mavx2], [AC_DEFINE(HAVE_AVX2, 1, Define to 1 if your compiler supports AVX2)])

AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"
LDFLAGS="$LDFLAGS $PTHREAD_CFLAGS"

# Prefer bash, needed for test.sh
AC_PATH_TOOL(BASH, bash, "/bin/bash")

# If GCC (or clang), turn on warnings.
if test "$ac_compiler_gnu" = yes; then
    CFLAGS="$CFLAGS -Wall -W"
    CXXFLAGS="$CXXFLAGS -Wall -W"
else
    CFLAGS="$CXXFLAGS -O"
    CXXFLAGS="$CXXFLAGS -O"
fi

AC_ARG_ENABLE(more_warnings,
  [AS_HELP_STRING([--enable-more-warnings],
    [enable more compiler warnings])])
if test "$dev_mode" = yes -o "$enable_more_warnings" = yes; then
    extra_warnings="-Wextra -Wpedantic"
    if test "$ac_compiler_clang" = yes; then
        extra_warnings="$extra_warnings -Weverything"
        AX_CHECK_COMPILE_FLAG([-Wno-c++2a-compat], [extra_warnings="$extra_warnings -Wno-c++2a-compat"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-c++98-compat-pedantic], [extra_warnings="$extra_warnings -Wno-c++98-compat-pedantic"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-c++98-compat], [extra_warnings="$extra_warnings -Wno-c++98-compat"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-constexpr-not-const], [extra_warnings="$extra_warnings -Wno-constexpr-not-const"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-conversion], [extra_warnings="$extra_warnings -Wno-conversion"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-disabled-macro-expansion], [extra_warnings="$extra_warnings -Wno-disabled-macro-expansion"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-documentation-unknown-command], [extra_warnings="$extra_warnings -Wno-documentation-unknown-command"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-exit-time-destructors], [extra_warnings="$extra_warnings -Wno-exit-time-destructors"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-format-nonliteral], [extra_warnings="$extra_warnings -Wno-format-nonliteral"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-global-constructors], [extra_warnings="$extra_warnings -Wno-global-constructors"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-implicit-fallthrough], [extra_warnings="$extra_warnings -Wno-implicit-fallthrough"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-padded], [extra_warnings="$extra_warnings -Wno-padded"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-return-std-move-in-c++11], [extra_warnings="$extra_warnings -Wno-return-std-move-in-c++11"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-shadow-field-in-constructor], [extra_warnings="$extra_warnings -Wno-shadow-field-in-constructor"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-shorten-64-to-32], [extra_warnings="$extra_warnings -Wno-shorten-64-to-32"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-sign-conversion], [extra_warnings="$extra_warnings -Wno-sign-conversion"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-undefined-func-template], [extra_warnings="$extra_warnings -Wno-undefined-func-template"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-weak-vtables], [extra_warnings="$extra_warnings -Wno-weak-vtables"],, [-Werror])

        # TODO: Enable these in the future:
        AX_CHECK_COMPILE_FLAG([-Wno-old-style-cast], [extra_warnings="$extra_warnings -Wno-old-style-cast"],, [-Werror])
        AX_CHECK_COMPILE_FLAG([-Wno-zero-as-null-pointer-constant], [extra_warnings="$extra_warnings -Wno-zero-as-null-pointer-constant"],, [-Werror])
    fi
fi

AC_HEADER_DIRENT
AC_HEADER_TIME
AC_HEADER_SYS_WAIT

AC_CHECK_TYPES(long long)

AC_CHECK_HEADERS(ctype.h pwd.h stdlib.h string.h strings.h sys/time.h sys/mman.h)
AC_CHECK_HEADERS(syslog.h)
AC_CHECK_HEADERS(termios.h)
AC_CHECK_HEADERS(sys/ioctl.h)
AC_CHECK_HEADERS(linux/fs.h)
AC_CHECK_HEADERS(sys/clonefile.h)

AC_CHECK_FUNCS(asctime_r)
AC_CHECK_FUNCS(getopt_long)
AC_CHECK_FUNCS(getpwuid)
AC_CHECK_FUNCS(gettimeofday)
AC_CHECK_FUNCS(localtime_r)
AC_CHECK_FUNCS(mkstemp)
AC_CHECK_FUNCS(realpath)
AC_CHECK_FUNCS(setenv)
AC_CHECK_FUNCS(strndup)
AC_CHECK_FUNCS(syslog)
AC_CHECK_FUNCS(unsetenv)
AC_CHECK_FUNCS(utimes)

dnl Check if -lm is needed.
AC_SEARCH_LIBS(cos, m)

dnl Check for libzstd
zstd_version=1.4.4
zstd_url=https://github.com/facebook/zstd/releases/download/v${zstd_version}/zstd-${zstd_version}.tar.gz

AC_ARG_WITH(libzstd-from-internet,
  [AS_HELP_STRING([--with-libzstd-from-internet],
    [download and use libzstd from the Internet instead of using the system's default libzstd])])
if test x${with_libzstd_from_internet} = x; then
    AC_CHECK_LIB(zstd, ZSTD_versionNumber, [found_libzstd=yes])
    if test x${found_libzstd} = x; then
        AC_MSG_ERROR(please install libzstd or use --with-libzstd-from-internet)
    fi
    LIBS="$LIBS -lzstd"
else
    zstd_archive=`basename ${zstd_url}`
    if ! test -f ${zstd_archive}; then
        AC_CHECK_TOOL(WGET, wget)
        if test -n "$WGET"; then
            AC_MSG_NOTICE(downloading libzstd from $zstd_url)
            wget -q ${zstd_url}
        else
            AC_CHECK_TOOL(CURL, curl)
            if test -n "$CURL"; then
                AC_MSG_NOTICE(downloading libzstd from $zstd_url)
                curl -sLOJ ${zstd_url}
            else
                AC_MSG_ERROR(please install wget or curl)
            fi
        fi
    fi
    if ! test -f $zstd_archive; then
        AC_MSG_ERROR(failed to download $zstd_url)
    fi
    (cd src/third_party && tar -xzf ../../${zstd_archive})
    rm -rf src/third_party/zstd
    mv src/third_party/zstd-${zstd_version} src/third_party/zstd
    CPPFLAGS="$CPPFLAGS -I$PWD/src/third_party/zstd/lib"
    extra_libs="src/third_party/zstd/lib/libzstd.a"
fi

dnl Check for libb2
libb2_version=0.98.1
libb2_url=https://github.com/BLAKE2/libb2/releases/download/v${libb2_version}/libb2-${libb2_version}.tar.gz

AC_ARG_WITH(libb2-from-internet,
  [AS_HELP_STRING([--with-libb2-from-internet],
    [download and use libb2 from the Internet instead of using the system's default libb2])])
if test x${with_libb2_from_internet} = x; then
    AC_CHECK_LIB([b2],[blake2b], [found_libb2=yes])
    if test x${found_libb2} = x; then
        AC_MSG_ERROR(please install libb2 or use --with-libb2-from-internet)
    fi
    LIBS="$LIBS -lb2"
else
    libb2_archive=`basename ${libb2_url}`
    if ! test -f ${libb2_archive}; then
        AC_CHECK_TOOL(WGET, wget)
        if test -n "$WGET"; then
            AC_MSG_NOTICE(downloading libb2 from $libb2_url)
            wget -q ${libb2_url}
        else
            AC_CHECK_TOOL(CURL, curl)
            if test -n "$CURL"; then
                AC_MSG_NOTICE(downloading libb2 from $libb2_url)
                curl -sLOJ ${libb2_url}
            else
                AC_MSG_ERROR(please install wget or curl)
            fi
        fi
    fi
    if ! test -f $libb2_archive; then
        AC_MSG_ERROR(failed to download $libb2_url)
    fi
    (cd src/third_party && tar -xzf ../../${libb2_archive})
    rm -rf src/third_party/libb2
    mv src/third_party/libb2-${libb2_version} src/third_party/libb2
    # try to make clang happy by changing the header guard macros
    sed -i -e 's/__BLAKE2_H__/BLAKE2_H/' src/third_party/libb2/src/blake2.h
    CPPFLAGS="$CPPFLAGS -I$PWD/src/third_party/libb2/src"
    extra_libs="$extra_libs src/third_party/libb2/src/.libs/libb2.a"
fi

if test "$cross_compiling" != no -o "$host_cpu" != 'x86_64'; then
    disable_cross=''
else
    disable_cross='#'
fi

AC_ARG_ENABLE(man,
  [AS_HELP_STRING([--disable-man],
    [disable installing man pages])])
if test x${enable_man} = xno; then
    disable_man='#'
fi

AC_ARG_ENABLE(tracing,
  [AS_HELP_STRING([--enable-tracing],
    [enable possibility to use internal ccache tracing])])
if test x${enable_tracing} = xyes; then
    CPPFLAGS="$CPPFLAGS -DMTR_ENABLED"
    extra_sources="src/third_party/minitrace.c"
fi

dnl Linking on Windows needs ws2_32
if test x${windows_os} = xyes; then
    LDFLAGS="$LDFLAGS -static"
    LIBS="$LIBS -lws2_32"
    AC_CHECK_FUNCS(GetFinalPathNameByHandleW,[],[LIBS="$LIBS -lpsapi"])
fi

AC_C_BIGENDIAN

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

if test ! -f $srcdir/src/version.cpp -a ! -f src/version.cpp ; then
    AC_MSG_WARN(unable to determine ccache version)
    echo "extern const char CCACHE_VERSION@<:@@:>@; const char CCACHE_VERSION@<:@@:>@ = \"unknown\";" >src/version.cpp
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

cat <<EOF >config.h.tmp
#pragma once
#ifdef __clang__
#  pragma clang diagnostic push
#  if __has_warning("-Wreserved-id-macro")
#    pragma clang diagnostic ignored "-Wreserved-id-macro"
#  endif
#endif

EOF
cat config.h >>config.h.tmp
cat <<EOF >>config.h.tmp

#ifdef __clang__
#  pragma clang diagnostic pop
#endif
EOF
mv config.h.tmp config.h

AC_MSG_NOTICE(now build ccache by running make)
