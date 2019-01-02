# _AC_LANG_COMPILER_CLANG
# ---------------------
# Check whether the compiler for the current language is clang.
# Adapted from standard autoconf function: _AC_LANG_COMPILER_GNU
#
# Note: clang also identifies itself as a GNU compiler (gcc 4.2.1)
# for compatibility reasons, so that cannot be used to determine
m4_define([_AC_LANG_COMPILER_CLANG],
[AC_CACHE_CHECK([whether we are using the clang _AC_LANG compiler],
                [ac_cv_[]_AC_LANG_ABBREV[]_compiler_clang],
[_AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [[#ifndef __clang__
       choke me
#endif
]])],
                   [ac_compiler_clang=yes],
                   [ac_compiler_clang=no])
ac_cv_[]_AC_LANG_ABBREV[]_compiler_clang=$ac_compiler_clang
])])# _AC_LANG_COMPILER_CLANG

