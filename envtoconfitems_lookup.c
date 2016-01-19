/* ANSI-C code produced by gperf version 3.0.4 */
/* Command-line: gperf envtoconfitems.gperf  */
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
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 9 "envtoconfitems.gperf"
struct env_to_conf_item;
/* maximum key range = 49, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
envtoconfitems_hash (register const char *str, register unsigned int len)
{
  static const unsigned char asso_values[] =
    {
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
       0, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 40, 20,  5,  5,
       0, 54,  0, 25, 54, 10, 20, 25, 20, 54,
       5, 54,  0,  0, 15, 25, 54, 54,  0,  5,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54, 54, 54, 54, 54,
      54, 54, 54, 54, 54, 54
    };
  return len + asso_values[(unsigned char)str[len - 1]] + asso_values[(unsigned char)str[0]];
}

static
#ifdef __GNUC__
__inline
#if defined __GNUC_STDC_INLINE__ || defined __GNUC_GNU_INLINE__
__attribute__ ((__gnu_inline__))
#endif
#endif
const struct env_to_conf_item *
envtoconfitems_get (register const char *str, register unsigned int len)
{
  enum
    {
      TOTAL_KEYWORDS = 32,
      MIN_WORD_LENGTH = 2,
      MAX_WORD_LENGTH = 18,
      MIN_HASH_VALUE = 5,
      MAX_HASH_VALUE = 53
    };

  static const struct env_to_conf_item wordlist[] =
    {
      {"",""}, {"",""}, {"",""}, {"",""}, {"",""},
#line 39 "envtoconfitems.gperf"
      {"STATS", "stats"},
      {"",""},
#line 23 "envtoconfitems.gperf"
      {"HASHDIR", "hash_dir"},
#line 17 "envtoconfitems.gperf"
      {"DIR", "cache_dir"},
#line 31 "envtoconfitems.gperf"
      {"PATH", "path"},
#line 38 "envtoconfitems.gperf"
      {"SLOPPINESS", "sloppiness"},
#line 32 "envtoconfitems.gperf"
      {"PREFIX", "prefix_command"},
#line 37 "envtoconfitems.gperf"
      {"RECACHE", "recache"},
#line 34 "envtoconfitems.gperf"
      {"READONLY", "read_only"},
      {"",""},
#line 21 "envtoconfitems.gperf"
      {"EXTRAFILES", "extra_files_to_hash"},
      {"",""},
#line 19 "envtoconfitems.gperf"
      {"DISABLE", "disable"},
#line 22 "envtoconfitems.gperf"
      {"HARDLINK", "hard_link"},
      {"",""},
#line 33 "envtoconfitems.gperf"
      {"PREFIX_CPP", "prefix_command_cpp"},
      {"",""},
#line 40 "envtoconfitems.gperf"
      {"TEMPDIR", "temporary_dir"},
#line 36 "envtoconfitems.gperf"
      {"READONLY_MEMCACHED", "read_only_memcached"},
#line 16 "envtoconfitems.gperf"
      {"CPP2", "run_second_cpp"},
      {"",""},
#line 18 "envtoconfitems.gperf"
      {"DIRECT", "direct_mode"},
#line 30 "envtoconfitems.gperf"
      {"NLEVELS", "cache_dir_levels"},
#line 14 "envtoconfitems.gperf"
      {"COMPRESS", "compression"},
      {"",""},
#line 35 "envtoconfitems.gperf"
      {"READONLY_DIRECT", "read_only_direct"},
      {"",""},
#line 25 "envtoconfitems.gperf"
      {"LOGFILE", "log_file"},
#line 26 "envtoconfitems.gperf"
      {"MAXFILES", "max_files"},
#line 20 "envtoconfitems.gperf"
      {"EXTENSION", "cpp_extension"},
#line 42 "envtoconfitems.gperf"
      {"UNIFY", "unify"},
      {"",""},
#line 27 "envtoconfitems.gperf"
      {"MAXSIZE", "max_size"},
#line 24 "envtoconfitems.gperf"
      {"IGNOREHEADERS", "ignore_headers_in_manifest"},
#line 28 "envtoconfitems.gperf"
      {"MEMCACHED_CONF", "memcached_conf"},
#line 41 "envtoconfitems.gperf"
      {"UMASK", "umask"},
      {"",""},
#line 12 "envtoconfitems.gperf"
      {"CC", "compiler"},
#line 13 "envtoconfitems.gperf"
      {"COMPILERCHECK", "compiler_check"},
#line 29 "envtoconfitems.gperf"
      {"MEMCACHED_ONLY", "memcached_only"},
      {"",""}, {"",""},
#line 11 "envtoconfitems.gperf"
      {"BASEDIR", "base_dir"},
      {"",""}, {"",""}, {"",""}, {"",""}, {"",""},
#line 15 "envtoconfitems.gperf"
      {"COMPRESSLEVEL", "compression_level"}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = envtoconfitems_hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register const char *s = wordlist[key].env_name;

          if (*str == *s && !strcmp (str + 1, s + 1))
            return &wordlist[key];
        }
    }
  return 0;
}
static const size_t ENVTOCONFITEMS_TOTAL_KEYWORDS = 32;
