/* ANSI-C code produced by gperf version 3.0.4 */
/* Command-line: gperf src/confitems.gperf  */
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

#line 8 "src/confitems.gperf"
struct conf_item;
/* maximum key range = 48, duplicates = 0 */

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
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52,  0, 35,  0,
       0, 10, 52,  0, 30, 25, 52,  0, 10, 20,
      10,  0,  0, 52,  5,  5, 10, 15, 52, 52,
      15, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52, 52, 52, 52, 52,
      52, 52, 52, 52, 52, 52
    };
  return len + asso_values[(unsigned char)str[1]] + asso_values[(unsigned char)str[0]];
}

static
#ifdef __GNUC__
__inline
#if defined __GNUC_STDC_INLINE__ || defined __GNUC_GNU_INLINE__
__attribute__ ((__gnu_inline__))
#endif
#endif
const struct conf_item *
confitems_get (register const char *str, register unsigned int len)
{
  enum
    {
      TOTAL_KEYWORDS = 33,
      MIN_WORD_LENGTH = 4,
      MAX_WORD_LENGTH = 26,
      MIN_HASH_VALUE = 4,
      MAX_HASH_VALUE = 51
    };

  static const struct conf_item wordlist[] =
    {
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
#line 30 "src/confitems.gperf"
      {"path",                20, ITEM(path, env_string)},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
      {"",0,NULL,0,NULL},
#line 13 "src/confitems.gperf"
      {"compiler",             3, ITEM(compiler, string)},
#line 11 "src/confitems.gperf"
      {"cache_dir",            1, ITEM(cache_dir, env_string)},
      {"",0,NULL,0,NULL},
#line 15 "src/confitems.gperf"
      {"compression",          5, ITEM(compression, bool)},
      {"",0,NULL,0,NULL},
#line 17 "src/confitems.gperf"
      {"cpp_extension",        7, ITEM(cpp_extension, string)},
#line 14 "src/confitems.gperf"
      {"compiler_check",       4, ITEM(compiler_check, string)},
#line 18 "src/confitems.gperf"
      {"debug",                8, ITEM(debug, bool)},
#line 12 "src/confitems.gperf"
      {"cache_dir_levels",     2, ITEM_V(cache_dir_levels, unsigned, dir_levels)},
#line 16 "src/confitems.gperf"
      {"compression_level",    6, ITEM(compression_level, unsigned)},
#line 27 "src/confitems.gperf"
      {"log_file",            17, ITEM(log_file, env_string)},
#line 32 "src/confitems.gperf"
      {"prefix_command",      22, ITEM(prefix_command, env_string)},
#line 39 "src/confitems.gperf"
      {"stats",               29, ITEM(stats, bool)},
#line 31 "src/confitems.gperf"
      {"pch_external_checksum", 21, ITEM(pch_external_checksum, bool)},
#line 36 "src/confitems.gperf"
      {"recache",             26, ITEM(recache, bool)},
#line 33 "src/confitems.gperf"
      {"prefix_command_cpp",  23, ITEM(prefix_command_cpp, env_string)},
#line 34 "src/confitems.gperf"
      {"read_only",           24, ITEM(read_only, bool)},
#line 38 "src/confitems.gperf"
      {"sloppiness",          28, ITEM(sloppiness, sloppiness)},
      {"",0,NULL,0,NULL},
#line 25 "src/confitems.gperf"
      {"keep_comments_cpp",   15, ITEM(keep_comments_cpp, bool)},
#line 29 "src/confitems.gperf"
      {"max_size",            19, ITEM(max_size, size)},
#line 28 "src/confitems.gperf"
      {"max_files",           18, ITEM(max_files, unsigned)},
#line 42 "src/confitems.gperf"
      {"unify",               32, ITEM(unify, bool)},
#line 35 "src/confitems.gperf"
      {"read_only_direct",    25, ITEM(read_only_direct, bool)},
#line 20 "src/confitems.gperf"
      {"disable",             10, ITEM(disable, bool)},
#line 40 "src/confitems.gperf"
      {"temporary_dir",       30, ITEM(temporary_dir, env_string)},
#line 37 "src/confitems.gperf"
      {"run_second_cpp",      27, ITEM(run_second_cpp, bool)},
      {"",0,NULL,0,NULL},
#line 19 "src/confitems.gperf"
      {"direct_mode",          9, ITEM(direct_mode, bool)},
      {"",0,NULL,0,NULL},
#line 23 "src/confitems.gperf"
      {"hash_dir",            13, ITEM(hash_dir, bool)},
#line 22 "src/confitems.gperf"
      {"hard_link",           12, ITEM(hard_link, bool)},
#line 41 "src/confitems.gperf"
      {"umask",               31, ITEM(umask, umask)},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
#line 10 "src/confitems.gperf"
      {"base_dir",             0, ITEM_V(base_dir, env_string, absolute_path)},
#line 21 "src/confitems.gperf"
      {"extra_files_to_hash", 11, ITEM(extra_files_to_hash, env_string)},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
      {"",0,NULL,0,NULL}, {"",0,NULL,0,NULL},
#line 26 "src/confitems.gperf"
      {"limit_multiple",      16, ITEM(limit_multiple, float)},
      {"",0,NULL,0,NULL},
#line 24 "src/confitems.gperf"
      {"ignore_headers_in_manifest", 14, ITEM(ignore_headers_in_manifest, env_string)}
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
static const size_t CONFITEMS_TOTAL_KEYWORDS = 33;
