// Copyright (C) 2010-2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include "language.hpp"

#include <ccache/util/filesystem.hpp>

namespace fs = util::filesystem;

namespace {

// Supported file extensions and corresponding languages (as in parameter to
// the -x option).
const struct
{
  const char* extension;
  const char* language;
} k_ext_lang_table[] = {
  {".c",    "c"                       },
  {".C",    "c++"                     },
  {".cc",   "c++"                     },
  {".CC",   "c++"                     },
  {".cp",   "c++"                     },
  {".CP",   "c++"                     },
  {".cpp",  "c++"                     },
  {".CPP",  "c++"                     },
  {".cxx",  "c++"                     },
  {".CXX",  "c++"                     },
  {".c++",  "c++"                     },
  {".C++",  "c++"                     },
  {".m",    "objective-c"             },
  {".M",    "objective-c++"           },
  {".mm",   "objective-c++"           },
  {".sx",   "assembler-with-cpp"      },
  {".S",    "assembler-with-cpp"      },
  // Preprocessed:
  {".i",    "cpp-output"              },
  {".ii",   "c++-cpp-output"          },
  {".mi",   "objective-c-cpp-output"  },
  {".mii",  "objective-c++-cpp-output"},
  {".s",    "assembler"               },
  // Header file (for precompilation):
  {".h",    "c-header"                },
  {".H",    "c++-header"              },
  {".h++",  "c++-header"              },
  {".H++",  "c++-header"              },
  {".hh",   "c++-header"              },
  {".HH",   "c++-header"              },
  {".hp",   "c++-header"              },
  {".HP",   "c++-header"              },
  {".hpp",  "c++-header"              },
  {".HPP",  "c++-header"              },
  {".hxx",  "c++-header"              },
  {".HXX",  "c++-header"              },
  {".tcc",  "c++-header"              },
  {".TCC",  "c++-header"              },
  {".cu",   "cu"                      }, // Special case in language_for_file: "cuda" for Clang
  {".hip",  "hip"                     },
  {nullptr, nullptr                   },
};

// Supported languages and their properties.
const struct
{
  const char* language;
  LanguageInfo info;
} k_language_info_table[] = {
  {"c",                        {.preprocessed = false}},
  {"cpp-output",               {.preprocessed = true} },
  {"c-header",                 {.preprocessed = false}},
  {"c++",                      {.preprocessed = false}},
  {"c++-cpp-output",           {.preprocessed = true} },
  {"c++-header",               {.preprocessed = false}},
  {"cu",                       {.preprocessed = false}}, // NVCC
  {"cuda",                     {.preprocessed = false}}, // Clang
  {"hip",                      {.preprocessed = false}},
  {"objective-c",              {.preprocessed = false}},
  {"objective-c-header",       {.preprocessed = false}},
  {"objc-cpp-output",          {.preprocessed = true} },
  {"objective-c-cpp-output",   {.preprocessed = true} },
  {"objective-c++",            {.preprocessed = false}},
  {"objc++-cpp-output",        {.preprocessed = true} },
  {"objective-c++-header",     {.preprocessed = false}},
  {"objective-c++-cpp-output", {.preprocessed = true} },
  {"assembler-with-cpp",       {.preprocessed = false}},
  {"assembler",                {.preprocessed = true} },
  {"ir",                       {.preprocessed = true} }, // Clang ThinLTO
  {nullptr,                    {}                     },
};

} // namespace

std::string_view
language_for_file(const fs::path& path, CompilerType compiler_type)
{
  const auto ext = path.extension();
  if (ext == ".cu" && compiler_type == CompilerType::clang) {
    // Special case: Clang maps .cu to cuda.
    return "cuda";
  }
  for (size_t i = 0; k_ext_lang_table[i].extension; ++i) {
    if (k_ext_lang_table[i].extension == ext) {
      return k_ext_lang_table[i].language;
    }
  }
  return {};
}

const LanguageInfo*
language_info_for_language(std::string_view language)
{
  for (size_t i = 0; k_language_info_table[i].language; ++i) {
    if (language == k_language_info_table[i].language) {
      return &k_language_info_table[i].info;
    }
  }
  return nullptr;
}
