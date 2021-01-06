// Copyright (C) 2019-2021 Joel Rosdahl and other contributors
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

#include "NonCopyable.hpp"
#include "Util.hpp"

#include "third_party/nonstd/optional.hpp"

#include <functional>
#include <limits>
#include <string>
#include <unordered_map>

enum class CompilerType { auto_guess, clang, gcc, nvcc, other, pump };

std::string compiler_type_to_string(CompilerType compiler_type);

class Config
{
public:
  Config() = default;
  Config(Config&) = default;
  Config& operator=(const Config&) = default;

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
  const std::string& debug_dir() const;
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
  double limit_multiple() const;
  const std::string& log_file() const;
  uint64_t max_files() const;
  uint64_t max_size() const;
  const std::string& path() const;
  bool pch_external_checksum() const;
  const std::string& prefix_command() const;
  const std::string& prefix_command_cpp() const;
  bool read_only() const;
  bool read_only_direct() const;
  bool recache() const;
  bool run_second_cpp() const;
  uint32_t sloppiness() const;
  bool stats() const;
  const std::string& temporary_dir() const;
  uint32_t umask() const;

  void set_base_dir(const std::string& value);
  void set_cache_dir(const std::string& value);
  void set_cpp_extension(const std::string& value);
  void set_compiler(const std::string& value);
  void set_compiler_type(CompilerType value);
  void set_depend_mode(bool value);
  void set_debug(bool value);
  void set_direct_mode(bool value);
  void set_ignore_options(const std::string& value);
  void set_inode_cache(bool value);
  void set_max_files(uint64_t value);
  void set_max_size(uint64_t value);
  void set_run_second_cpp(bool value);

  // Where to write configuration changes.
  const std::string& primary_config_path() const;
  // Secondary, read-only configuration file (if any).
  const std::string& secondary_config_path() const;

  void set_primary_config_path(std::string path);
  void set_secondary_config_path(std::string path);

  using ItemVisitor = std::function<void(const std::string& key,
                                         const std::string& value,
                                         const std::string& origin)>;

  // Set config values from a configuration file.
  //
  // Returns false if the file can't be opened, otherwise true. Throws Error on
  // invalid configuration values.
  bool update_from_file(const std::string& path);

  // Set config values from environment variables.
  //
  // Throws Error on invalid configuration values.
  void update_from_environment();

  // Get a config value in string form given a key.
  std::string get_string_value(const std::string& key) const;

  void visit_items(const ItemVisitor& item_visitor) const;

  static void set_value_in_file(const std::string& path,
                                const std::string& key,
                                const std::string& value);

  // Called from unit tests.
  static void check_key_tables_consistency();

private:
  std::string m_primary_config_path;
  std::string m_secondary_config_path;

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
  std::string m_debug_dir;
  bool m_depend_mode = false;
  bool m_direct_mode = true;
  bool m_disable = false;
  std::string m_extra_files_to_hash;
  bool m_file_clone = false;
  bool m_hard_link = false;
  bool m_hash_dir = true;
  std::string m_ignore_headers_in_manifest;
  std::string m_ignore_options;
  bool m_inode_cache = false;
  bool m_keep_comments_cpp = false;
  double m_limit_multiple = 0.8;
  std::string m_log_file;
  uint64_t m_max_files = 0;
  uint64_t m_max_size = 5ULL * 1000 * 1000 * 1000;
  std::string m_path;
  bool m_pch_external_checksum = false;
  std::string m_prefix_command;
  std::string m_prefix_command_cpp;
  bool m_read_only = false;
  bool m_read_only_direct = false;
  bool m_recache = false;
  bool m_run_second_cpp = true;
  uint32_t m_sloppiness = 0;
  bool m_stats = true;
  std::string m_temporary_dir;
  uint32_t m_umask = std::numeric_limits<uint32_t>::max(); // Don't set umask

  bool m_temporary_dir_configured_explicitly = false;

  std::unordered_map<std::string /*key*/, std::string /*origin*/> m_origins;

  void set_item(const std::string& key,
                const std::string& value,
                const nonstd::optional<std::string>& env_var_key,
                bool negate,
                const std::string& origin);

  static std::string default_temporary_dir(const std::string& cache_dir);
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

inline const std::string&
Config::debug_dir() const
{
  return m_debug_dir;
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

inline double
Config::limit_multiple() const
{
  return m_limit_multiple;
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
Config::run_second_cpp() const
{
  return m_run_second_cpp;
}

inline uint32_t
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
Config::temporary_dir() const
{
  return m_temporary_dir;
}

inline uint32_t
Config::umask() const
{
  return m_umask;
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
    m_temporary_dir = default_temporary_dir(m_cache_dir);
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
Config::set_max_size(uint64_t value)
{
  m_max_size = value;
}

inline void
Config::set_run_second_cpp(bool value)
{
  m_run_second_cpp = value;
}
