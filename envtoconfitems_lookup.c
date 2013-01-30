/* ANSI-C code produced by gperf version 3.0.3 */
/* Command-line: gperf envtoconfitems.gperf  */
/* Computed positions: -k'1,5' */

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
/* maximum key range = 41, duplicates = 0 */

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
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43,  5,  0,  0, 10,
      20, 43, 15, 43, 10, 43, 20, 10, 15,  0,
       5,  5,  5,  0,  0,  5, 43, 43, 43, 43,
       0, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43, 43, 43, 43,
      43, 43, 43, 43, 43, 43, 43
    };
  register int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[4]+1];
      /*FALLTHROUGH*/
      case 4:
      case 3:
      case 2:
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval;
}

static
#ifdef __GNUC__
__inline
#ifdef __GNUC_STDC_INLINE__
__attribute__ ((__gnu_inline__))
#endif
#endif
const struct env_to_conf_item *
envtoconfitems_get (register const char *str, register unsigned int len)
{
  enum
    {
      TOTAL_KEYWORDS = 26,
      MIN_WORD_LENGTH = 2,
      MAX_WORD_LENGTH = 13,
      MIN_HASH_VALUE = 2,
      MAX_HASH_VALUE = 42
    };

  static const struct env_to_conf_item wordlist[] =
    {
      {"",""}, {"",""},
#line 12 "envtoconfitems.gperf"
      {"CC", "compiler"},
#line 17 "envtoconfitems.gperf"
      {"DIR", "cache_dir"},
#line 16 "envtoconfitems.gperf"
      {"CPP2", "run_second_cpp"},
#line 33 "envtoconfitems.gperf"
      {"STATS", "stats"},
#line 18 "envtoconfitems.gperf"
      {"DIRECT", "direct_mode"},
#line 19 "envtoconfitems.gperf"
      {"DISABLE", "disable"},
#line 14 "envtoconfitems.gperf"
      {"COMPRESS", "compression"},
#line 28 "envtoconfitems.gperf"
      {"PATH", "path"},
#line 36 "envtoconfitems.gperf"
      {"UNIFY", "unify"},
      {"",""},
#line 31 "envtoconfitems.gperf"
      {"RECACHE", "recache"},
#line 15 "envtoconfitems.gperf"
      {"COMPRESSLEVEL", "compression_level"},
      {"",""},
#line 32 "envtoconfitems.gperf"
      {"SLOPPINESS", "sloppiness"},
      {"",""},
#line 34 "envtoconfitems.gperf"
      {"TEMPDIR", "temporary_dir"},
#line 30 "envtoconfitems.gperf"
      {"READONLY", "read_only"},
#line 20 "envtoconfitems.gperf"
      {"EXTENSION", "cpp_extension"},
      {"",""},
#line 29 "envtoconfitems.gperf"
      {"PREFIX", "prefix_command"},
#line 11 "envtoconfitems.gperf"
      {"BASEDIR", "base_dir"},
#line 13 "envtoconfitems.gperf"
      {"COMPILERCHECK", "compiler_check"},
      {"",""},
#line 21 "envtoconfitems.gperf"
      {"EXTRAFILES", "extra_files_to_hash"},
      {"",""},
#line 26 "envtoconfitems.gperf"
      {"MAXSIZE", "max_size"},
#line 25 "envtoconfitems.gperf"
      {"MAXFILES", "max_files"},
      {"",""},
#line 35 "envtoconfitems.gperf"
      {"UMASK", "umask"},
      {"",""},
#line 23 "envtoconfitems.gperf"
      {"HASHDIR", "hash_dir"},
#line 22 "envtoconfitems.gperf"
      {"HARDLINK", "hard_link"},
      {"",""}, {"",""}, {"",""},
#line 24 "envtoconfitems.gperf"
      {"LOGFILE", "log_file"},
      {"",""}, {"",""}, {"",""}, {"",""},
#line 27 "envtoconfitems.gperf"
      {"NLEVELS", "cache_dir_levels"}
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
static const size_t ENVTOCONFITEMS_TOTAL_KEYWORDS = 26;
