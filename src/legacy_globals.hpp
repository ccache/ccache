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

#include "ccache.hpp"
#include "hash.hpp"

#include <string>
#include <unordered_map>

// variable descriptions are in the .cpp file

extern char* current_working_dir;
extern char* stats_file;
extern unsigned lock_staleness_limit;

extern struct args* orig_args;

extern char* output_dia;

extern char* output_dwo;

extern const char* actual_language;

#define MAX_ARCH_ARGS 10
extern size_t arch_args_size;
extern char* arch_args[MAX_ARCH_ARGS];

extern struct digest* cached_result_name;

extern char* cached_result_path;

extern char* manifest_path;

extern time_t time_of_compilation;

extern std::unordered_map<std::string, digest> g_included_files;

extern bool has_absolute_include_headers;

extern char** ignore_headers;

extern size_t ignore_headers_len;

extern bool generating_dependencies;

extern bool generating_coverage;

extern bool generating_stackusage;

extern bool generating_diagnostics;

extern bool seen_split_dwarf;

extern bool profile_arcs;

extern char* profile_dir;

extern char* i_tmpfile;

extern bool direct_i_file;

extern char* cpp_stderr;

extern char* manifest_stats_file;

extern bool output_is_precompiled_header;

extern enum guessed_compiler guessed_compiler;

extern bool profile_use;
extern bool profile_generate;

extern bool using_precompiled_header;

extern char* included_pch_file;
