// Copyright (C) 2021-2023 Joel Rosdahl and other contributors
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

namespace core {

// Statistics fields in storage order.
enum class Statistic {
  none = 0,
  compiler_produced_stdout = 1,
  compile_failed = 2,
  internal_error = 3,
  cache_miss = 4,
  preprocessor_error = 5,
  could_not_find_compiler = 6,
  missing_cache_file = 7,
  preprocessed_cache_hit = 8,
  bad_compiler_arguments = 9,
  called_for_link = 10,
  files_in_cache = 11,
  cache_size_kibibyte = 12,
  obsolete_max_files = 13,
  obsolete_max_size = 14,
  unsupported_source_language = 15,
  bad_output_file = 16,
  no_input_file = 17,
  multiple_source_files = 18,
  autoconf_test = 19,
  unsupported_compiler_option = 20,
  output_to_stdout = 21,
  direct_cache_hit = 22,
  compiler_produced_no_output = 23,
  compiler_produced_empty_output = 24,
  error_hashing_extra_file = 25,
  compiler_check_failed = 26,
  could_not_use_precompiled_header = 27,
  called_for_preprocessing = 28,
  cleanups_performed = 29,
  unsupported_code_directive = 30,
  stats_zeroed_timestamp = 31,
  could_not_use_modules = 32,
  direct_cache_miss = 33,
  preprocessed_cache_miss = 34,
  local_storage_read_hit = 35,
  local_storage_read_miss = 36,
  remote_storage_read_hit = 37,
  remote_storage_read_miss = 38,
  remote_storage_error = 39,
  remote_storage_timeout = 40,
  recache = 41,
  unsupported_environment_variable = 42,
  local_storage_write = 43,
  local_storage_hit = 44,
  local_storage_miss = 45,
  remote_storage_write = 46,
  remote_storage_hit = 47,
  remote_storage_miss = 48,

  // 49-64: files in level 2 subdirs 0-f
  subdir_files_base = 49,

  // 65-80: size (KiB) in level 2 subdirs 0-f
  subdir_size_kibibyte_base = 65,

  disabled = 81,
  END = 82
};

} // namespace core
