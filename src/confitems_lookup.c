/* ANSI-C code produced by gperf version 3.1 */
/* Command-line: gperf src/confitems.gperf  */
/* Computed positions: -k'1,$' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."
#endif

#line 8 "src/confitems.gperf"

#include "confitems.h"
#include "conf.h"

#undef bool
#define ITEM_ENTRY(name, type, verify_fn) \
	offsetof(struct conf, name), confitem_parse_ ## type, \
	confitem_format_ ## type, verify_fn
#define ITEM(name, type) \
	ITEM_ENTRY(name, type, NULL)
#define ITEM_V(name, type, verification) \
	ITEM_ENTRY(name, type, confitem_verify_ ## verification)
#line 21 "src/confitems.gperf"
struct conf_item;
/* maximum key range = 65, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
confitems_hash (register const char *str, register size_t len)
{
  static const unsigned char asso_values[] =
    {
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70,  5, 20,
       5,  0, 30,  5, 30, 10, 70, 20, 25,  0,
      10, 70,  0, 70,  0,  0, 10,  0, 70, 70,
      70, 55, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70, 70, 70, 70, 70,
      70, 70, 70, 70, 70, 70
    };
  return len + asso_values[(unsigned char)str[len - 1]] + asso_values[(unsigned char)str[0]];
}

const struct conf_item *
confitems_get (register const char *str, register size_t len)
{
  enum
    {
      TOTAL_KEYWORDS = 36,
      MIN_WORD_LENGTH = 4,
      MAX_WORD_LENGTH = 26,
      MIN_HASH_VALUE = 5,
      MAX_HASH_VALUE = 69
    };

  static const struct conf_item wordlist[] =
    {
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
      {"",0,0,NULL,NULL,NULL},
#line 55 "src/confitems.gperf"
      {"stats",               32, ITEM(stats, bool)},
      {"",0,0,NULL,NULL,NULL},
#line 52 "src/confitems.gperf"
      {"recache",             29, ITEM(recache, bool)},
#line 42 "src/confitems.gperf"
      {"max_size",            19, ITEM(max_size, size)},
#line 41 "src/confitems.gperf"
      {"max_files",           18, ITEM(max_files, unsigned)},
#line 54 "src/confitems.gperf"
      {"sloppiness",          31, ITEM(sloppiness, sloppiness)},
      {"",0,0,NULL,NULL,NULL},
#line 33 "src/confitems.gperf"
      {"disable",             10, ITEM(disable, bool)},
#line 23 "src/confitems.gperf"
      {"base_dir",             0, ITEM_V(base_dir, env_string, absolute_path)},
#line 53 "src/confitems.gperf"
      {"run_second_cpp",      30, ITEM(run_second_cpp, bool)},
#line 31 "src/confitems.gperf"
      {"debug",                8, ITEM(debug, bool)},
#line 32 "src/confitems.gperf"
      {"direct_mode",          9, ITEM(direct_mode, bool)},
      {"",0,0,NULL,NULL,NULL},
#line 48 "src/confitems.gperf"
      {"prefix_command_cpp",  25, ITEM(prefix_command_cpp, env_string)},
#line 47 "src/confitems.gperf"
      {"prefix_command",      24, ITEM(prefix_command, env_string)},
      {"",0,0,NULL,NULL,NULL},
#line 46 "src/confitems.gperf"
      {"pch_external_checksum", 23, ITEM(pch_external_checksum, bool)},
      {"",0,0,NULL,NULL,NULL},
#line 56 "src/confitems.gperf"
      {"temporary_dir",       33, ITEM(temporary_dir, env_string)},
#line 51 "src/confitems.gperf"
      {"read_only_memcached", 28, ITEM(read_only_memcached, bool)},
#line 57 "src/confitems.gperf"
      {"umask",               34, ITEM(umask, umask)},
#line 50 "src/confitems.gperf"
      {"read_only_direct",    27, ITEM(read_only_direct, bool)},
      {"",0,0,NULL,NULL,NULL},
#line 26 "src/confitems.gperf"
      {"compiler",             3, ITEM(compiler, string)},
#line 24 "src/confitems.gperf"
      {"cache_dir",            1, ITEM(cache_dir, env_string)},
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
      {"",0,0,NULL,NULL,NULL},
#line 40 "src/confitems.gperf"
      {"log_file",            17, ITEM(log_file, env_string)},
#line 45 "src/confitems.gperf"
      {"path",                22, ITEM(path, env_string)},
      {"",0,0,NULL,NULL,NULL},
#line 25 "src/confitems.gperf"
      {"cache_dir_levels",     2, ITEM_V(cache_dir_levels, unsigned, dir_levels)},
#line 38 "src/confitems.gperf"
      {"keep_comments_cpp",   15, ITEM(keep_comments_cpp, bool)},
#line 36 "src/confitems.gperf"
      {"hash_dir",            13, ITEM(hash_dir, bool)},
#line 39 "src/confitems.gperf"
      {"limit_multiple",      16, ITEM(limit_multiple, double)},
      {"",0,0,NULL,NULL,NULL},
#line 28 "src/confitems.gperf"
      {"compression",          5, ITEM(compression, bool)},
      {"",0,0,NULL,NULL,NULL},
#line 30 "src/confitems.gperf"
      {"cpp_extension",        7, ITEM(cpp_extension, string)},
#line 43 "src/confitems.gperf"
      {"memcached_conf",      20, ITEM(memcached_conf, string)},
      {"",0,0,NULL,NULL,NULL},
#line 37 "src/confitems.gperf"
      {"ignore_headers_in_manifest", 14, ITEM(ignore_headers_in_manifest, env_string)},
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
#line 34 "src/confitems.gperf"
      {"extra_files_to_hash", 11, ITEM(extra_files_to_hash, env_string)},
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
#line 27 "src/confitems.gperf"
      {"compiler_check",       4, ITEM(compiler_check, string)},
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
#line 35 "src/confitems.gperf"
      {"hard_link",           12, ITEM(hard_link, bool)},
#line 58 "src/confitems.gperf"
      {"unify",               35, ITEM(unify, bool)},
      {"",0,0,NULL,NULL,NULL},
#line 29 "src/confitems.gperf"
      {"compression_level",    6, ITEM(compression_level, unsigned)},
      {"",0,0,NULL,NULL,NULL},
#line 49 "src/confitems.gperf"
      {"read_only",           26, ITEM(read_only, bool)},
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
      {"",0,0,NULL,NULL,NULL}, {"",0,0,NULL,NULL,NULL},
#line 44 "src/confitems.gperf"
      {"memcached_only",      21, ITEM(memcached_only, bool)}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register unsigned int key = confitems_hash (str, len);

      if (key <= MAX_HASH_VALUE)
        {
          register const char *s = wordlist[key].name;

          if (*str == *s && !strcmp (str + 1, s + 1))
            return &wordlist[key];
        }
    }
  return 0;
}
size_t confitems_count(void) { return 36; }
