// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "compopt.hpp"

#include "third_party/fmt/core.h"

// The option it too hard to handle at all.
#define TOO_HARD (1 << 0)

// The option it too hard for the direct mode.
#define TOO_HARD_DIRECT (1 << 1)

// The option takes a separate argument, e.g. "-D FOO=1".
#define TAKES_ARG (1 << 2)

// The option takes a concatenated argument, e.g. "-DFOO=1".
#define TAKES_CONCAT_ARG (1 << 3)

// The argument to the option is a path that may be rewritten if base_dir is
// used.
#define TAKES_PATH (1 << 4)

// The option only affects preprocessing; not passed to the compiler if
// run_second_cpp is false.
#define AFFECTS_CPP (1 << 5)

// The option only affects compilation; not passed to the preprocessor.
#define AFFECTS_COMP (1 << 6)

struct compopt
{
  const char* name;
  int type;
};

const struct compopt compopts[] = {
  {"--Werror", TAKES_ARG},                            // nvcc
  {"--analyze", TOO_HARD},                            // Clang
  {"--compiler-bindir", AFFECTS_CPP | TAKES_ARG},     // nvcc
  {"--libdevice-directory", AFFECTS_CPP | TAKES_ARG}, // nvcc
  {"--output-directory", AFFECTS_CPP | TAKES_ARG},    // nvcc
  {"--param", TAKES_ARG},
  {"--save-temps", TOO_HARD},
  {"--save-temps=cwd", TOO_HARD},
  {"--save-temps=obj", TOO_HARD},
  {"--serialize-diagnostics", TAKES_ARG | TAKES_PATH},
  {"-A", TAKES_ARG},
  {"-B", TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-D", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG},
  {"-E", TOO_HARD},
  {"-F", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-G", TAKES_ARG},
  {"-I", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-L", TAKES_ARG},
  {"-M", TOO_HARD},
  {"-MF", TAKES_ARG},
  {"-MJ", TAKES_ARG | TOO_HARD},
  {"-MM", TOO_HARD},
  {"-MQ", TAKES_ARG},
  {"-MT", TAKES_ARG},
  {"-P", TOO_HARD},
  {"-U", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG},
  {"-V", TAKES_ARG},
  {"-Wa,", TAKES_CONCAT_ARG | AFFECTS_COMP},
  {"-Werror", AFFECTS_COMP}, // don't exit with error when preprocessing
  {"-Wl,", TAKES_CONCAT_ARG | AFFECTS_COMP},
  {"-Wno-error", AFFECTS_COMP},
  {"-Xassembler", TAKES_ARG | TAKES_CONCAT_ARG | AFFECTS_COMP},
  {"-Xclang", TAKES_ARG},
  {"-Xlinker", TAKES_ARG | TAKES_CONCAT_ARG | AFFECTS_COMP},
  {"-Xpreprocessor", AFFECTS_CPP | TOO_HARD_DIRECT | TAKES_ARG},
  {"-all_load", AFFECTS_COMP},
  {"-analyze", TOO_HARD}, // Clang
  {"-arch", TAKES_ARG},
  {"-aux-info", TAKES_ARG},
  {"-b", TAKES_ARG},
  {"-bind_at_load", AFFECTS_COMP},
  {"-bundle", AFFECTS_COMP},
  {"-ccbin", AFFECTS_CPP | TAKES_ARG}, // nvcc
  {"-emit-pch", AFFECTS_COMP},         // Clang
  {"-emit-pth", AFFECTS_COMP},         // Clang
  {"-fno-working-directory", AFFECTS_CPP},
  {"-fplugin=libcc1plugin", TOO_HARD}, // interaction with GDB
  {"-frepo", TOO_HARD},
  {"-ftime-trace", TOO_HARD}, // Clang
  {"-fworking-directory", AFFECTS_CPP},
  {"-gtoggle", TOO_HARD},
  {"-idirafter", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-iframework", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-imacros", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-imultilib", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-include", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-include-pch", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-include-pth", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-install_name", TAKES_ARG}, // Darwin linker option
  {"-iprefix", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-iquote", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-isysroot", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-isystem", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-iwithprefix", AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-iwithprefixbefore",
   AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
  {"-ldir", AFFECTS_CPP | TAKES_ARG}, // nvcc
  {"-nolibc", AFFECTS_COMP},
  {"-nostdinc", AFFECTS_CPP},
  {"-nostdinc++", AFFECTS_CPP},
  {"-odir", AFFECTS_CPP | TAKES_ARG}, // nvcc
  {"-pie", AFFECTS_COMP},
  {"-prebind", AFFECTS_COMP},
  {"-preload", AFFECTS_COMP},
  {"-rdynamic", AFFECTS_COMP},
  {"-remap", AFFECTS_CPP},
  {"-save-temps", TOO_HARD},
  {"-save-temps=cwd", TOO_HARD},
  {"-save-temps=obj", TOO_HARD},
  {"-stdlib=", AFFECTS_CPP | TAKES_CONCAT_ARG},
  {"-trigraphs", AFFECTS_CPP},
  {"-u", TAKES_ARG | TAKES_CONCAT_ARG},
};

static int
compare_compopts(const void* key1, const void* key2)
{
  const struct compopt* opt1 = static_cast<const struct compopt*>(key1);
  const struct compopt* opt2 = static_cast<const struct compopt*>(key2);
  return strcmp(opt1->name, opt2->name);
}

static int
compare_prefix_compopts(const void* key1, const void* key2)
{
  const struct compopt* opt1 = static_cast<const struct compopt*>(key1);
  const struct compopt* opt2 = static_cast<const struct compopt*>(key2);
  return strncmp(opt1->name, opt2->name, strlen(opt2->name));
}

static const struct compopt*
find(const std::string& option)
{
  struct compopt key;
  key.name = option.c_str();
  void* result = bsearch(&key,
                         compopts,
                         ARRAY_SIZE(compopts),
                         sizeof(compopts[0]),
                         compare_compopts);
  return static_cast<compopt*>(result);
}

static const struct compopt*
find_prefix(const std::string& option)
{
  struct compopt key;
  key.name = option.c_str();
  void* result = bsearch(&key,
                         compopts,
                         ARRAY_SIZE(compopts),
                         sizeof(compopts[0]),
                         compare_prefix_compopts);
  return static_cast<compopt*>(result);
}

// Runs fn on the first two characters of option.
bool
compopt_short(bool (*fn)(const std::string&), const std::string& option)
{
  bool retval = fn(option.substr(0, 2));
  return retval;
}

// Used by unittest/test_compopt.cpp.
bool compopt_verify_sortedness_and_flags();

// For test purposes.
bool
compopt_verify_sortedness_and_flags()
{
  for (size_t i = 0; i < ARRAY_SIZE(compopts); i++) {
    if (compopts[i].type & TOO_HARD && compopts[i].type & TAKES_CONCAT_ARG) {
      fmt::print(stderr,
                 "type (TOO_HARD | TAKES_CONCAT_ARG) not allowed, used by {}\n",
                 compopts[i].name);
      return false;
    }

    if (i == 0) {
      continue;
    }

    if (strcmp(compopts[i - 1].name, compopts[i].name) >= 0) {
      fmt::print(stderr,
                 "compopt_verify_sortedness: {} >= {}\n",
                 compopts[i - 1].name,
                 compopts[i].name);
      return false;
    }
  }
  return true;
}

bool
compopt_affects_cpp(const std::string& option)
{
  const struct compopt* co = find(option);
  return co && (co->type & AFFECTS_CPP);
}

bool
compopt_affects_comp(const std::string& option)
{
  const struct compopt* co = find(option);
  return co && (co->type & AFFECTS_COMP);
}

bool
compopt_too_hard(const std::string& option)
{
  const struct compopt* co = find(option);
  return co && (co->type & TOO_HARD);
}

bool
compopt_too_hard_for_direct_mode(const std::string& option)
{
  const struct compopt* co = find(option);
  return co && (co->type & TOO_HARD_DIRECT);
}

bool
compopt_takes_path(const std::string& option)
{
  const struct compopt* co = find(option);
  return co && (co->type & TAKES_PATH);
}

bool
compopt_takes_arg(const std::string& option)
{
  const struct compopt* co = find(option);
  return co && (co->type & TAKES_ARG);
}

bool
compopt_takes_concat_arg(const std::string& option)
{
  const struct compopt* co = find(option);
  return co && (co->type & TAKES_CONCAT_ARG);
}

// Determines if the prefix of the option matches any option and affects the
// preprocessor.
bool
compopt_prefix_affects_cpp(const std::string& option)
{
  // Prefix options have to take concatenated args.
  const struct compopt* co = find_prefix(option);
  return co && (co->type & TAKES_CONCAT_ARG) && (co->type & AFFECTS_CPP);
}

// Determines if the prefix of the option matches any option and affects the
// preprocessor.
bool
compopt_prefix_affects_comp(const std::string& option)
{
  // Prefix options have to take concatenated args.
  const struct compopt* co = find_prefix(option);
  return co && (co->type & TAKES_CONCAT_ARG) && (co->type & AFFECTS_COMP);
}
