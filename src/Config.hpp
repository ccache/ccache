// Copyright (C) 2019-2023 Joel Rosdahl and other contributors
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

#include <core/Sloppiness.hpp>
#include <util/NonCopyable.hpp>
#include <util/filesystem.hpp>
#include <util/string.hpp>

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class CompilerType {
  auto_guess,
  clang,
  clang_cl,
  gcc,
  icl,
  msvc,
  nvcc,
  ti,
  other
};

std::string compiler_type_to_string(CompilerType compiler_type);

class Config : util::NonCopyable
{
public:
  Config() = default;

  void read(const std::vector<std::string>& cmdline_config_settings = {});

  bool absolute_paths_in_stderr() const;
  const std::string& base_dir() const;
  const std::string& cache_dir() const;
  const std::string& compiler() const;
  const std::string& compiler_check() const;
  CompilerType compiler_type() const;
  bool compression() const;
  int8_t compression_level() const;
  const std::string& cpp_extension() const;
  bool debug() const;
  const std::filesystem::path& debug_dir() const;
  uint8_t debug_level() const;
  bool depend_mode() const;
  bool direct_mode() const;
  bool disable() const;
  const std::string& extra_files_to_hash() const;
  bool file_clone() const;
  bool hard_link() const;
  bool hash_dir() const;
  const std::string& ignore_headers_in_manifest() const;
  const std::string& ignore_options() const;
  bool inode_cache() const;
  bool keep_comments_cpp() const;
  const std::string& log_file() const;
  uint64_t max_files() const;
  uint64_t max_size() const;
  const std::string& msvc_dep_prefix() const;
  const std::string& path() const;
  bool pch_external_checksum() const;
  const std::string& prefix_command() const;
  const std::string& prefix_command_cpp() const;
  bool read_only() const;
  bool read_only_direct() const;
  bool recache() const;
  bool remote_only() const;
  const std::string& remote_storage() const;
  bool reshare() const;
  bool run_second_cpp() const;
  core::Sloppiness sloppiness() const;
  bool stats() const;
  const std::string& stats_log() const;
  const std::string& namespace_() const;
  const std::string& temporary_dir() const;
  std::optional<mode_t> umask() const;

  // Return true for Clang and clang-cl.
  bool is_compiler_group_clang() const;

  // Return true for MSVC (cl.exe), clang-cl, and icl.
  bool is_compiler_group_msvc() const;

  util::SizeUnitPrefixType size_unit_prefix_type() const;
  std::string default_temporary_dir() const;

  void set_base_dir(const std::string& value);
  void set_cache_dir(const std::string& value);
  void set_compiler(const std::string& value);
  void set_compiler_type(CompilerType value);
  void set_cpp_extension(const std::string& value);
  void set_debug(bool value);
  void set_depend_mode(bool value);
  void set_direct_mode(bool value);
  void set_file_clone(bool value);
  void set_hard_link(bool value);
  void set_ignore_options(const std::string& value);
  void set_inode_cache(bool value);
  void set_max_files(uint64_t value);
  void set_msvc_dep_prefix(const std::string& value);
  void set_run_second_cpp(bool value);
  void set_temporary_dir(const std::string& value);

  // Where to write configuration changes.
  const std::string& config_path() const;
  // System (read-only) configuration file (if any).
  const std::string& system_config_path() const;

  void set_config_path(std::string path);
  void set_system_config_path(std::string path);

  using ItemVisitor = std::function<void(const std::string& key,
                                         const std::string& value,
                                         const std::string& origin)>;

  // Set config values from a configuration file.
  //
  // Returns false if the file can't be opened, otherwise true. Throws Error on
  // invalid configuration values.
  bool update_from_file(const std::string& path);

  // Set config values from a map with key-value pairs.
  //
  // Throws Error on invalid configuration values.
  void update_from_map(const std::unordered_map<std::string, std::string>& map);

  // Set config values from environment variables.
  //
  // Throws Error on invalid configuration values.
  void update_from_environment();

  // Get a config value in string form given a key.
  std::string get_string_value(const std::string& key) const;

  void visit_items(const ItemVisitor& item_visitor) const;

  void set_value_in_file(const std::string& path,
                         const std::string& key,
                         const std::string& value) const;

  // Called from unit tests.
  static void check_key_tables_consistency();

private:
  std::string m_config_path;
  std::string m_system_config_path;

  bool m_absolute_paths_in_stderr = false;
  std::string m_base_dir;
  std::string m_cache_dir;
  std::string m_compiler;
  std::string m_compiler_check = "mtime";
  CompilerType m_compiler_type = CompilerType::auto_guess;
  bool m_compression = true;
  int8_t m_compression_level = 0; // Use default level
  std::string m_cpp_extension;
  bool m_debug = false;
  std::filesystem::path m_debug_dir;
  uint8_t m_debug_level = 2;
  bool m_depend_mode = false;
  bool m_direct_mode = true;
  bool m_disable = false;
  std::string m_extra_files_to_hash;
  bool m_file_clone = false;
  bool m_hard_link = false;
  bool m_hash_dir = true;
  std::string m_ignore_headers_in_manifest;
  std::string m_ignore_options;
  bool m_inode_cache = true;
  bool m_keep_comments_cpp = false;
  std::string m_log_file;
  uint64_t m_max_files = 0;
  uint64_t m_max_size = 5ULL * 1024 * 1024 * 1024;
  std::string m_msvc_dep_prefix = "Note: including file:";
  std::string m_path;
  bool m_pch_external_checksum = false;
  std::string m_prefix_command;
  std::string m_prefix_command_cpp;
  bool m_read_only = false;
  bool m_read_only_direct = false;
  bool m_recache = false;
  bool m_reshare = false;
  bool m_run_second_cpp = true;
  bool m_remote_only = false;
  std::string m_remote_storage;
  core::Sloppiness m_sloppiness;
  bool m_stats = true;
  std::string m_stats_log;
  std::string m_namespace;
  std::string m_temporary_dir;
  std::optional<mode_t> m_umask;

  bool m_temporary_dir_configured_explicitly = false;
  util::SizeUnitPrefixType m_size_prefix_type =
    util::SizeUnitPrefixType::binary;

  std::unordered_map<std::string /*key*/, std::string /*origin*/> m_origins;

  void set_item(const std::string& key,
                const std::string& unexpanded_value,
                const std::optional<std::string>& env_var_key,
                bool negate,
                const std::string& origin);
};

inline bool
Config::absolute_paths_in_stderr() const
{
  return m_absolute_paths_in_stderr;
}

inline const std::string&
Config::base_dir() const
{
  return m_base_dir;
}

inline const std::string&
Config::cache_dir() const
{
  return m_cache_dir;
}

inline const std::string&
Config::compiler() const
{
  return m_compiler;
}

inline const std::string&
Config::compiler_check() const
{
  return m_compiler_check;
}

inline CompilerType
Config::compiler_type() const
{
  return m_compiler_type;
}

inline bool
Config::is_compiler_group_clang() const
{
  return m_compiler_type == CompilerType::clang
         || m_compiler_type == CompilerType::clang_cl;
}

inline bool
Config::is_compiler_group_msvc() const
{
  return m_compiler_type == CompilerType::msvc
         || m_compiler_type == CompilerType::clang_cl
         || m_compiler_type == CompilerType::icl;
}

inline bool
Config::compression() const
{
  return m_compression;
}

inline int8_t
Config::compression_level() const
{
  return m_compression_level;
}

inline const std::string&
Config::cpp_extension() const
{
  return m_cpp_extension;
}

inline bool
Config::debug() const
{
  return m_debug;
}

inline const std::filesystem::path&
Config::debug_dir() const
{
  return m_debug_dir;
}

inline uint8_t
Config::debug_level() const
{
  return m_debug_level;
}

inline bool
Config::depend_mode() const
{
  return m_depend_mode;
}

inline bool
Config::direct_mode() const
{
  return m_direct_mode;
}

inline bool
Config::disable() const
{
  return m_disable;
}

inline const std::string&
Config::extra_files_to_hash() const
{
  return m_extra_files_to_hash;
}

inline bool
Config::file_clone() const
{
  return m_file_clone;
}

inline bool
Config::hard_link() const
{
  return m_hard_link;
}

inline bool
Config::hash_dir() const
{
  return m_hash_dir;
}

inline const std::string&
Config::ignore_headers_in_manifest() const
{
  return m_ignore_headers_in_manifest;
}

inline const std::string&
Config::ignore_options() const
{
  return m_ignore_options;
}

inline bool
Config::inode_cache() const
{
  return m_inode_cache;
}

inline bool
Config::keep_comments_cpp() const
{
  return m_keep_comments_cpp;
}

inline const std::string&
Config::log_file() const
{
  return m_log_file;
}

inline uint64_t
Config::max_files() const
{
  return m_max_files;
}

inline uint64_t
Config::max_size() const
{
  return m_max_size;
}

inline const std::string&
Config::msvc_dep_prefix() const
{
  return m_msvc_dep_prefix;
}

inline const std::string&
Config::path() const
{
  return m_path;
}

inline bool
Config::pch_external_checksum() const
{
  return m_pch_external_checksum;
}

inline const std::string&
Config::prefix_command() const
{
  return m_prefix_command;
}

inline const std::string&
Config::prefix_command_cpp() const
{
  return m_prefix_command_cpp;
}

inline bool
Config::read_only() const
{
  return m_read_only;
}

inline bool
Config::read_only_direct() const
{
  return m_read_only_direct;
}

inline bool
Config::recache() const
{
  return m_recache;
}

inline bool
Config::reshare() const
{
  return m_reshare;
}

inline bool
Config::run_second_cpp() const
{
  return m_run_second_cpp;
}

inline bool
Config::remote_only() const
{
  return m_remote_only;
}

inline const std::string&
Config::remote_storage() const
{
  return m_remote_storage;
}

inline core::Sloppiness
Config::sloppiness() const
{
  return m_sloppiness;
}

inline bool
Config::stats() const
{
  return m_stats;
}

inline const std::string&
Config::stats_log() const
{
  return m_stats_log;
}

inline const std::string&
Config::namespace_() const
{
  return m_namespace;
}

inline const std::string&
Config::temporary_dir() const
{
  return m_temporary_dir;
}

inline std::optional<mode_t>
Config::umask() const
{
  return m_umask;
}

inline util::SizeUnitPrefixType
Config::size_unit_prefix_type() const
{
  return m_size_prefix_type;
}

inline void
Config::set_base_dir(const std::string& value)
{
  m_base_dir = value;
}

inline void
Config::set_cache_dir(const std::string& value)
{
  m_cache_dir = value;
  if (!m_temporary_dir_configured_explicitly) {
    m_temporary_dir = default_temporary_dir();
  }
}

inline void
Config::set_cpp_extension(const std::string& value)
{
  m_cpp_extension = value;
}

inline void
Config::set_compiler(const std::string& value)
{
  m_compiler = value;
}

inline void
Config::set_compiler_type(CompilerType value)
{
  m_compiler_type = value;
}

inline void
Config::set_depend_mode(bool value)
{
  m_depend_mode = value;
}

inline void
Config::set_debug(bool value)
{
  m_debug = value;
}

inline void
Config::set_direct_mode(bool value)
{
  m_direct_mode = value;
}

inline void
Config::set_file_clone(const bool value)
{
  m_file_clone = value;
}

inline void
Config::set_hard_link(const bool value)
{
  m_hard_link = value;
}

inline void
Config::set_ignore_options(const std::string& value)
{
  m_ignore_options = value;
}

inline void
Config::set_inode_cache(bool value)
{
  m_inode_cache = value;
}

inline void
Config::set_max_files(uint64_t value)
{
  m_max_files = value;
}

inline void
Config::set_msvc_dep_prefix(const std::string& value)
{
  m_msvc_dep_prefix = value;
}

inline void
Config::set_run_second_cpp(bool value)
{
  m_run_second_cpp = value;
}

inline void
Config::set_temporary_dir(const std::string& value)
{
  m_temporary_dir = value;
}
