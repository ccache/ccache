project('ccache', 'c', version : '3.4.2', meson_version : '>= 0.37.1')

cc = meson.get_compiler('c')

zlib_dep = dependency ('zlib', required : false)
m_dep = cc.find_library('m', required : false)
winsock2_dep = cc.find_library('ws2_32', required : false)

# config.h generation
config_h_inc = include_directories('.')
config_h_flag = ['-DHAVE_CONFIG_H']
ccache_conf = configuration_data()

ccache_conf.set('CCACHE_VERSION', '"@0@"'.format (meson.project_version()))

if host_machine.system() != 'windows'
  ccache_conf.set10('_GNU_SOURCE', true)
endif

check_headers = [
  'sys/mman.h',
  'sys/wait.h',
  'sys/time.h',
  'sys/file.h',
  'pwd.h',
  'inttypes.h',
  'locale.h',
  'stdarg.h',
  'termios.h',
  'dirent.h',
  'unistd.h',
  'strings.h',
  'utime.h',
  'stddef.h',
  'stdint.h',
  'stdlib.h',
  'inttypes.h',
]

foreach h : check_headers
  if cc.has_header(h)
    define = 'HAVE_' + h.underscorify().to_upper()
    ccache_conf.set10(define, define)
  endif
endforeach

check_functions = [
  'strtok_r',
  'vsnprintf',
  'snprintf',
  'vasprintf',
  'asprintf',
  'mkstemp',
  'gettimeofday',
  'gethostname',
  'strndup',
  'unsetenv',
  'realpath',
  'GetFinalPathNameByHandleW',
  'getpwuid',
  'utimes',
  'getopt_long',
  'localeconv',
  'va_copy',
  '__va_copy',
]

foreach f : check_functions
  if cc.has_function(f)
    define = 'HAVE_' + f.underscorify().to_upper()
    ccache_conf.set10(define, define)
  endif
endforeach

struct_members = [
  [ 'lconv', 'decimal_point', '#include <locale.h>'],
  [ 'lconv', 'thousands_sep', '#include <locale.h>'],
]

foreach m : struct_members
  struct = m[0]
  member = m[1]
  header_check_prefix = m[2]
  if cc.has_member('struct ' + struct, member, prefix : header_check_prefix)
    define = 'HAVE_@0@_@1@'.format(struct.to_upper(), member.underscorify().to_upper())
    ccache_conf.set(define, 1)
  endif
endforeach

configure_file(
  output : 'config.h',
  input: 'config.h.meson',
  configuration : ccache_conf
)

subdir('src')
subdir('unittest')
