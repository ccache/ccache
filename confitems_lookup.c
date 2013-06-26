/* ANSI-C code produced by gperf version 3.0.3 */
/* Command-line: gperf confitems.gperf  */
/* Computed positions: -k'1-2' */

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
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 8 "confitems.gperf"
struct conf_item;
/* maximum key range = 41, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
confitems_hash (register const char *str, register unsigned int len)
{
  static const unsigned char asso_values[] =
    {
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45,  0, 20,  0,
      15, 10, 45, 45, 13, 10, 45, 45, 25, 10,
       0,  0,  0, 45, 10,  0,  0, 15, 45, 45,
      15, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45, 45, 45, 45, 45,
      45, 45, 45, 45, 45, 45
    };
  return len + asso_values[(unsigned char)str[1]] + asso_values[(unsigned char)str[0]];
}

#ifdef __GNUC__
__inline
#ifdef __GNUC_STDC_INLINE__
__attribute__ ((__gnu_inline__))
#endif
#endif
const struct conf_item *
confitems_get (register const char *str, register unsigned int len)
{
  enum
    {
      TOTAL_KEYWORDS = 27,
      MIN_WORD_LENGTH = 4,
      MAX_WORD_LENGTH = 19,
      MIN_HASH_VALUE = 4,
      MAX_HASH_VALUE = 44
    };

  static const struct conf_item wordlist[] =
    {
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
#line 26 "confitems.gperf"
      {"path",                16, ITEM(path, env_string)},
#line 32 "confitems.gperf"
      {"stats",               22, ITEM(stats, bool)},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
#line 13 "confitems.gperf"
      {"compiler",             3, ITEM(compiler, string)},
#line 11 "confitems.gperf"
      {"cache_dir",            1, ITEM(cache_dir, env_string)},
      {"",0,NULL,0,NULL},
#line 15 "confitems.gperf"
      {"compression",          5, ITEM(compression, bool)},
      {"",0,NULL,0,NULL},
#line 17 "confitems.gperf"
      {"cpp_extension",        7, ITEM(cpp_extension, string)},
#line 14 "confitems.gperf"
      {"compiler_check",       4, ITEM(compiler_check, string)},
      {"",0,NULL,0,NULL},
#line 12 "confitems.gperf"
      {"cache_dir_levels",     2, ITEM_V(cache_dir_levels, unsigned, dir_levels)},
#line 16 "confitems.gperf"
      {"compression_level",    6, ITEM(compression_level, unsigned)},
#line 25 "confitems.gperf"
      {"max_size",            15, ITEM(max_size, size)},
#line 24 "confitems.gperf"
      {"max_files",           14, ITEM(max_files, unsigned)},
#line 35 "confitems.gperf"
      {"unify",               25, ITEM(unify, bool)},
#line 22 "confitems.gperf"
      {"hash_dir",            12, ITEM(hash_dir, bool)},
#line 21 "confitems.gperf"
      {"hard_link",           11, ITEM(hard_link, bool)},
#line 33 "confitems.gperf"
      {"temporary_dir",       23, ITEM(temporary_dir, env_string)},
#line 27 "confitems.gperf"
      {"prefix_command",      17, ITEM(prefix_command, env_string)},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
#line 29 "confitems.gperf"
      {"recache",             19, ITEM(recache, bool)},
#line 10 "confitems.gperf"
      {"base_dir",             0, ITEM_V(base_dir, env_string, absolute_path)},
#line 28 "confitems.gperf"
      {"read_only",           18, ITEM(read_only, bool)},
#line 34 "confitems.gperf"
      {"umask",               24, ITEM(umask, umask)},
      {"",0,NULL,0,NULL},
#line 19 "confitems.gperf"
      {"disable",              9, ITEM(disable, bool)},
#line 23 "confitems.gperf"
      {"log_file",            13, ITEM(log_file, env_string)},
#line 36 "confitems.gperf"
      {"memcached_conf",      26, ITEM(memcached_conf, string)},
#line 31 "confitems.gperf"
      {"sloppiness",          21, ITEM(sloppiness, sloppiness)},
#line 18 "confitems.gperf"
      {"direct_mode",          8, ITEM(direct_mode, bool)},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
#line 30 "confitems.gperf"
      {"run_second_cpp",      20, ITEM(run_second_cpp, bool)},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
#line 20 "confitems.gperf"
      {"extra_files_to_hash", 10, ITEM(extra_files_to_hash, env_string)}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = confitems_hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register const char *s = wordlist[key].name;

          if (*str == *s && !strcmp (str + 1, s + 1))
            return &wordlist[key];
        }
    }
  return 0;
}
static const size_t CONFITEMS_TOTAL_KEYWORDS = 27;
