// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#pragma once

#include <ccache/util/args.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// This class holds meta-information derived from the compiler arguments.
struct ArgsInfo
{
  // The source file path.
  std::filesystem::path orig_input_file;

  // The source file path, potentially rewritten into relative.
  std::filesystem::path input_file;

  // Prefix to the input file when adding it to a command line.
  std::string input_file_prefix;

  // In normal compiler operation an output file is created if there is no
  // compiler error. However certain flags like -fsyntax-only change this
  // behavior.
  bool expect_output_obj = true;

  // The output file being compiled to.
  std::filesystem::path orig_output_obj;

  // The output file being compiled to, potentially rewritten into relative.
  std::filesystem::path output_obj;

  // The path to the dependency file (implicit or specified with -MFdepfile,
  // -Wp,-MD,depfile or -Wp,-MMD,depfile).
  std::filesystem::path output_dep;

  // The path to the stack usage (implicit when using -fstack-usage).
  std::filesystem::path output_su;

  // The path to the callgraph info (implicit when using -fcallgraph-info).
  std::filesystem::path output_ci;

  // Diagnostic generation information (Clang). Contains pathname if not empty.
  std::filesystem::path output_dia;

  // Split dwarf information (GCC 4.8 and up). Contains pathname if not empty.
  std::filesystem::path output_dwo;

  // The path to the ipa clones (implicit when using -fdump-ipa-clones).
  std::filesystem::path output_ipa;

  // Assembler listing file.
  std::filesystem::path output_al;

  // The given PCH filepath being compiled to (by -Fp option).
  std::filesystem::path orig_included_pch_file;

  // The .gch/.pch/.pth file or directory used for compilation.
  std::filesystem::path included_pch_file;

  // Language to use for the compilation target (see language.c).
  std::string actual_language;

  // Is the compiler being asked to output debug info?
  bool generating_debuginfo = false;

  // Is the compiler being asked to output dependencies?
  bool generating_dependencies = false;

  // Is the compiler being asked to output includes (MSVC /showIncludes or
  // clang-cl /showIncludes:user)?
  bool generating_includes = false;

  // The dependency target in the dependency file (the object file unless
  // overridden via e.g. -MT or -MQ).
  std::optional<std::string> dependency_target;

  // Is the compiler being asked to output coverage?
  bool generating_coverage = false;

  // Is the compiler being asked to output stack usage?
  bool generating_stackusage = false;

  // -fdump-ipa-clones
  bool generating_ipa_clones = false;

  // -fcallgraph-info
  bool generating_callgraphinfo = false;

  // Is the compiler being asked to generate diagnostics
  // (--serialize-diagnostics)?
  bool generating_diagnostics = false;

  // Are we generating a pch file (msvc -Yc)?
  bool generating_pch = false;

  // Whether to strip color codes from diagnostic messages on output.
  bool strip_diagnostics_colors = false;

  // Have we seen --?
  bool seen_double_dash = false;

  // Have we seen -gsplit-dwarf?
  bool seen_split_dwarf = false;

  // Are we compiling a .i or .ii file directly?
  bool direct_i_file = false;

  // Whether the output is a precompiled header.
  bool output_is_precompiled_header = false;

  // Is the compiler being asked to output coverage data (.gcda) at runtime?
  bool profile_arcs = false;

  // Name of the custom profile directory or file.
  std::filesystem::path profile_path;

  // Path to the root of the source tree during a profile build.
  // This path will be stripped from the beginning of the mangled absolute paths
  // in the gcda file name
  std::filesystem::path profile_prefix_path;

  // Profile generation / usage information.
  bool profile_use = false;
  bool profile_generate = false;

  // Whether we are using a precompiled header (either via -include, #include or
  // Clang's -include-pch or -include-pth).
  bool using_precompiled_header = false;

  // Whether Clang is instructed not to include timestamps in the precompiled
  // header it generates.
  bool fno_pch_timestamp = false;

  // Files referenced by -fsanitize-blacklist options.
  std::vector<std::filesystem::path> sanitize_blacklists;

  // Architectures from -arch options.
  std::vector<std::string> arch_args;

  // Values for -Xarch_* options.
  std::unordered_map<std::string, std::vector<std::string>> xarch_args;

  // Relocating debuginfo in the format old=new.
  std::vector<std::string> debug_prefix_maps;

  // Compilation directory as passed in -ffile-compilation-dir or
  // -fdebug-compilation-dir.
  std::string compilation_dir;

  // Build session file as passed in -fbuild-session-file.
  std::filesystem::path build_session_file;
};
