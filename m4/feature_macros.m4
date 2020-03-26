dnl ===========================================================================
dnl Feature macro stuff borrowed from Python's configure.in
dnl
dnl For license information, see
dnl <http://www.python.org/download/releases/2.6.2/license/>.
dnl ===========================================================================

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
  AIX/5|AIX/7)
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
    SunOS/5.11)
      ;;
    *)
      AC_DEFINE(_XOPEN_SOURCE, 700,
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
    SunOS/5.10|SunOS/5.11)
      AC_DEFINE(__EXTENSIONS__, 1,
                Define to activate Unix95-and-earlier features)
      ;;
    *)
      AC_DEFINE(_XOPEN_SOURCE_EXTENDED, 1,
                Define to activate Unix95-and-earlier features)
      ;;
  esac

  AC_DEFINE(_POSIX_C_SOURCE, 200809L, Define to activate features from IEEE Stds 1003.1-2001)

fi
