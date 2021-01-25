include(CheckIncludeFile)
set(include_files
    linux/fs.h
    pwd.h
    sys/clonefile.h
    sys/ioctl.h
    sys/mman.h
    sys/time.h
    sys/wait.h
    sys/file.h
    syslog.h
    termios.h
    dirent.h
    strings.h
    unistd.h
    utime.h
    sys/utime.h
    varargs.h)
foreach(include_file IN ITEMS ${include_files})
  string(TOUPPER ${include_file} include_var)
  string(REGEX REPLACE "[/.]" "_" include_var ${include_var})
  set(include_var HAVE_${include_var})
  check_include_file(${include_file} ${include_var})
endforeach()

include(CheckFunctionExists)
set(functions
    asctime_r
    geteuid
    getopt_long
    getpwuid
    gettimeofday
    posix_fallocate
    realpath
    setenv
    strndup
    syslog
    unsetenv
    utimes)
foreach(func IN ITEMS ${functions})
  string(TOUPPER ${func} func_var)
  set(func_var HAVE_${func_var})
  check_function_exists(${func} ${func_var})
endforeach()

include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_LINK_OPTIONS -pthread)
check_c_source_compiles(
  [=[
    #include <pthread.h>
    int main()
    {
      pthread_mutexattr_t attr;
      (void)pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
      return 0;
    }
  ]=]
  HAVE_PTHREAD_MUTEX_ROBUST)
check_function_exists(pthread_mutexattr_setpshared HAVE_PTHREAD_MUTEXATTR_SETPSHARED)
set(CMAKE_REQUIRED_LINK_OPTIONS)

include(CheckStructHasMember)
check_struct_has_member("struct stat" st_ctim sys/stat.h
                        HAVE_STRUCT_STAT_ST_CTIM)
check_struct_has_member("struct stat" st_mtim sys/stat.h
                        HAVE_STRUCT_STAT_ST_MTIM)
check_struct_has_member("struct statfs" f_fstypename sys/mount.h
                        HAVE_STRUCT_STATFS_F_FSTYPENAME)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
  [=[
    #include <immintrin.h>
    void func() __attribute__((target("avx2")));
    void func() { _mm256_abs_epi8(_mm256_set1_epi32(42)); }
    int main()
    {
      func();
      return 0;
    }
  ]=]
  HAVE_AVX2)

list(APPEND CMAKE_REQUIRED_LIBRARIES ws2_32)
list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES ws2_32)

include(CheckTypeSize)
check_type_size("long long" HAVE_LONG_LONG)

if(WIN32)
  set(_WIN32_WINNT 0x0600)
endif()

if(CMAKE_SYSTEM MATCHES "Darwin")
  set(_DARWIN_C_SOURCE 1)
endif()

# alias
set(MTR_ENABLED "${ENABLE_TRACING}")

configure_file(${CMAKE_SOURCE_DIR}/cmake/config.h.in
               ${CMAKE_BINARY_DIR}/config.h @ONLY)
