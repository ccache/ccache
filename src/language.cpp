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

#include "language.hpp"

#include "Util.hpp"

namespace {

// Supported file extensions and corresponding languages (as in parameter to
// the -x option).
const struct
{
  const char* extension;
  const char* language;
} k_ext_lang_table[] = {
  {".c", "c"},
  {".C", "c++"},
  {".cc", "c++"},
  {".CC", "c++"},
  {".cp", "c++"},
  {".CP", "c++"},
  {".cpp", "c++"},
  {".CPP", "c++"},
  {".cxx", "c++"},
  {".CXX", "c++"},
  {".c++", "c++"},
  {".C++", "c++"},
  {".m", "objective-c"},
  {".M", "objective-c++"},
  {".mm", "objective-c++"},
  {".sx", "assembler-with-cpp"},
  {".S", "assembler-with-cpp"},
  // Preprocessed:
  {".i", "cpp-output"},
  {".ii", "c++-cpp-output"},
  {".mi", "objective-c-cpp-output"},
  {".mii", "objective-c++-cpp-output"},
  {".s", "assembler"},
  // Header file (for precompilation):
  {".h", "c-header"},
  {".H", "c++-header"},
  {".h++", "c++-header"},
  {".H++", "c++-header"},
  {".hh", "c++-header"},
  {".HH", "c++-header"},
  {".hp", "c++-header"},
  {".HP", "c++-header"},
  {".hpp", "c++-header"},
  {".HPP", "c++-header"},
  {".hxx", "c++-header"},
  {".HXX", "c++-header"},
  {".tcc", "c++-header"},
  {".TCC", "c++-header"},
  {".cu", "cu"},
  {nullptr, nullptr},
};

// Supported languages and corresponding preprocessed languages.
const struct
{
  const char* language;
  const char* p_language;
} k_lang_p_lang_table[] = {
  {"c", "cpp-output"},
  {"cpp-output", "cpp-output"},
  {"c-header", "cpp-output"},
  {"c++", "c++-cpp-output"},
  {"c++-cpp-output", "c++-cpp-output"},
  {"c++-header", "c++-cpp-output"},
  {"cu", "cpp-output"},
  {"objective-c", "objective-c-cpp-output"},
  {"objective-c-header", "objective-c-cpp-output"},
  {"objc-cpp-output", "objective-c-cpp-output"},
  {"objective-c-cpp-output", "objective-c-cpp-output"},
  {"objective-c++", "objective-c++-cpp-output"},
  {"objc++-cpp-output", "objective-c++-cpp-output"},
  {"objective-c++-header", "objective-c++-cpp-output"},
  {"objective-c++-cpp-output", "objective-c++-cpp-output"},
  {"assembler-with-cpp", "assembler"},
  {"assembler", "assembler"},
  {nullptr, nullptr},
};

} // namespace

std::string
language_for_file(const std::string& fname)
{
  auto ext = Util::get_extension(fname);
  for (size_t i = 0; k_ext_lang_table[i].extension; ++i) {
    if (k_ext_lang_table[i].extension == ext) {
      return k_ext_lang_table[i].language;
    }
  }
  return {};
}

std::string
p_language_for_language(const std::string& language)
{
  for (size_t i = 0; k_lang_p_lang_table[i].language; ++i) {
    if (language == k_lang_p_lang_table[i].language) {
      return k_lang_p_lang_table[i].p_language;
    }
  }
  return {};
}

std::string
extension_for_language(const std::string& language)
{
  for (size_t i = 0; k_ext_lang_table[i].extension; i++) {
    if (language == k_ext_lang_table[i].language) {
      return k_ext_lang_table[i].extension;
    }
  }
  return {};
}

bool
language_is_supported(const std::string& language)
{
  return !p_language_for_language(language).empty();
}

bool
language_is_preprocessed(const std::string& language)
{
  return language == p_language_for_language(language);
}
