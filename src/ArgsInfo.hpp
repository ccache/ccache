// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#pragma once

#include "system.hpp"

#include <string>

// linker error with -flto otherwise (as static constexpr)
constexpr int ArgsInfo_max_arch_args = 10;

// This class holds meta-information derived from the compiler arguments.
struct ArgsInfo
{
  // The source file.
  std::string input_file;

  // The output file being compiled to.
  std::string output_obj;

  // The path to the dependency file (implicit or specified with -MF).
  std::string output_dep;

  // The path to the coverage file (implicit when using -ftest-coverage).
  std::string output_cov;

  // The path to the stack usage (implicit when using -fstack-usage).
  std::string output_su;

  // Diagnostic generation information (clang). Contains pathname if not empty.
  std::string output_dia;

  // Split dwarf information (GCC 4.8 and up). Contains pathname if not empty.
  std::string output_dwo;

  // Language to use for the compilation target (see language.c).
  std::string actual_language;

  // Is the compiler being asked to output debug info?
  bool generating_debuginfo = false;

  // Is the compiler being asked to output dependencies?
  bool generating_dependencies = false;

  // Is the compiler being asked to output coverage?
  bool generating_coverage = false;

  // Is the compiler being asked to output stack usage?
  bool generating_stackusage = false;

  // Us the compiler being asked to generate diagnostics
  // (--serialize-diagnostics)?
  bool generating_diagnostics = false;

  // Have we seen -gsplit-dwarf?
  bool seen_split_dwarf = false;

  // Are we compiling a .i or .ii file directly?
  bool direct_i_file = false;

  // Whether the output is a precompiled header.
  bool output_is_precompiled_header = false;

  // Is the compiler being asked to output coverage data (.gcda) at runtime?
  bool profile_arcs = false;

  // Name of the custom profile directory (default: object dirname).
  std::string profile_dir;

  // Profile generation / usage information.
  bool profile_use = false;
  bool profile_generate = false;

  // Whether we are using a precompiled header (either via -include, #include or
  // clang's -include-pch or -include-pth).
  bool using_precompiled_header = false;

  // Sanitize blacklist
  char** sanitize_blacklists = nullptr;
  size_t sanitize_blacklists_len = 0;

  // Array for storing -arch options.
  size_t arch_args_size = 0;
  char* arch_args[ArgsInfo_max_arch_args] = {nullptr};

  // Relocating debuginfo in the format old=new.
  char** debug_prefix_maps = nullptr;
  size_t debug_prefix_maps_len = 0;

  // Argument list to add to compiler invocation in depend mode.
  struct args* depend_extra_args = nullptr;

  ArgsInfo() = default;
  ~ArgsInfo();

  ArgsInfo(const ArgsInfo&) = delete;
  ArgsInfo& operator=(const ArgsInfo&) = delete;

  ArgsInfo(ArgsInfo&&) = delete;
  ArgsInfo& operator=(ArgsInfo&&) = delete;
};
