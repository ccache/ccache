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

#include "Config.hpp"

#include "AtomicFile.hpp"
#include "Compression.hpp"
#include "Sloppiness.hpp"
#include "Util.hpp"
#include "assertions.hpp"
#include "exceptions.hpp"
#include "fmtmacros.hpp"

#include "third_party/fmt/core.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using nonstd::nullopt;
using nonstd::optional;

namespace {

enum class ConfigItem {
  absolute_paths_in_stderr,
  base_dir,
  cache_dir,
  compiler,
  compiler_check,
  compiler_type,
  compression,
  compression_level,
  cpp_extension,
  debug,
  debug_dir,
  depend_mode,
  direct_mode,
  disable,
  extra_files_to_hash,
  file_clone,
  hard_link,
  hash_dir,
  ignore_headers_in_manifest,
  ignore_options,
  inode_cache,
  keep_comments_cpp,
  limit_multiple,
  log_file,
  max_files,
  max_size,
  path,
  pch_external_checksum,
  prefix_command,
  prefix_command_cpp,
  read_only,
  read_only_direct,
  recache,
  run_second_cpp,
  sloppiness,
  stats,
  temporary_dir,
  umask,
};

const std::unordered_map<std::string, ConfigItem> k_config_key_table = {
  {"absolute_paths_in_stderr", ConfigItem::absolute_paths_in_stderr},
  {"base_dir", ConfigItem::base_dir},
  {"cache_dir", ConfigItem::cache_dir},
  {"compiler", ConfigItem::compiler},
  {"compiler_check", ConfigItem::compiler_check},
  {"compiler_type", ConfigItem::compiler_type},
  {"compression", ConfigItem::compression},
  {"compression_level", ConfigItem::compression_level},
  {"cpp_extension", ConfigItem::cpp_extension},
  {"debug", ConfigItem::debug},
  {"debug_dir", ConfigItem::debug_dir},
  {"depend_mode", ConfigItem::depend_mode},
  {"direct_mode", ConfigItem::direct_mode},
  {"disable", ConfigItem::disable},
  {"extra_files_to_hash", ConfigItem::extra_files_to_hash},
  {"file_clone", ConfigItem::file_clone},
  {"hard_link", ConfigItem::hard_link},
  {"hash_dir", ConfigItem::hash_dir},
  {"ignore_headers_in_manifest", ConfigItem::ignore_headers_in_manifest},
  {"ignore_options", ConfigItem::ignore_options},
  {"inode_cache", ConfigItem::inode_cache},
  {"keep_comments_cpp", ConfigItem::keep_comments_cpp},
  {"limit_multiple", ConfigItem::limit_multiple},
  {"log_file", ConfigItem::log_file},
  {"max_files", ConfigItem::max_files},
  {"max_size", ConfigItem::max_size},
  {"path", ConfigItem::path},
  {"pch_external_checksum", ConfigItem::pch_external_checksum},
  {"prefix_command", ConfigItem::prefix_command},
  {"prefix_command_cpp", ConfigItem::prefix_command_cpp},
  {"read_only", ConfigItem::read_only},
  {"read_only_direct", ConfigItem::read_only_direct},
  {"recache", ConfigItem::recache},
  {"run_second_cpp", ConfigItem::run_second_cpp},
  {"sloppiness", ConfigItem::sloppiness},
  {"stats", ConfigItem::stats},
  {"temporary_dir", ConfigItem::temporary_dir},
  {"umask", ConfigItem::umask},
};

const std::unordered_map<std::string, std::string> k_env_variable_table = {
  {"ABSSTDERR", "absolute_paths_in_stderr"},
  {"BASEDIR", "base_dir"},
  {"CC", "compiler"}, // Alias for CCACHE_COMPILER
  {"COMMENTS", "keep_comments_cpp"},
  {"COMPILER", "compiler"},
  {"COMPILERCHECK", "compiler_check"},
  {"COMPILERTYPE", "compiler_type"},
  {"COMPRESS", "compression"},
  {"COMPRESSLEVEL", "compression_level"},
  {"CPP2", "run_second_cpp"},
  {"DEBUG", "debug"},
  {"DEBUGDIR", "debug_dir"},
  {"DEPEND", "depend_mode"},
  {"DIR", "cache_dir"},
  {"DIRECT", "direct_mode"},
  {"DISABLE", "disable"},
  {"EXTENSION", "cpp_extension"},
  {"EXTRAFILES", "extra_files_to_hash"},
  {"FILECLONE", "file_clone"},
  {"HARDLINK", "hard_link"},
  {"HASHDIR", "hash_dir"},
  {"IGNOREHEADERS", "ignore_headers_in_manifest"},
  {"IGNOREOPTIONS", "ignore_options"},
  {"INODECACHE", "inode_cache"},
  {"LIMIT_MULTIPLE", "limit_multiple"},
  {"LOGFILE", "log_file"},
  {"MAXFILES", "max_files"},
  {"MAXSIZE", "max_size"},
  {"PATH", "path"},
  {"PCH_EXTSUM", "pch_external_checksum"},
  {"PREFIX", "prefix_command"},
  {"PREFIX_CPP", "prefix_command_cpp"},
  {"READONLY", "read_only"},
  {"READONLY_DIRECT", "read_only_direct"},
  {"RECACHE", "recache"},
  {"SLOPPINESS", "sloppiness"},
  {"STATS", "stats"},
  {"TEMPDIR", "temporary_dir"},
  {"UMASK", "umask"},
};

bool
parse_bool(const std::string& value,
           const optional<std::string> env_var_key,
           bool negate)
{
  if (env_var_key) {
    // Special rule for boolean settings from the environment: "0", "false",
    // "disable" and "no" (case insensitive) are invalid, and all other values
    // mean true.
    //
    // Previously any value meant true, but this was surprising to users, who
    // might do something like CCACHE_DISABLE=0 and expect ccache to be
    // enabled.
    std::string lower_value = Util::to_lowercase(value);
    if (value == "0" || lower_value == "false" || lower_value == "disable"
        || lower_value == "no") {
      throw Error(
        "invalid boolean environment variable value \"{}\" (did you mean to"
        " set \"CCACHE_{}{}=true\"?)",
        value,
        negate ? "" : "NO",
        *env_var_key);
    }
    return !negate;
  } else if (value == "true") {
    return true;
  } else if (value == "false") {
    return false;
  } else {
    throw Error("not a boolean value: \"{}\"", value);
  }
}

std::string
format_bool(bool value)
{
  return value ? "true" : "false";
}

double
parse_double(const std::string& value)
{
  size_t end;
  double result;
  try {
    result = std::stod(value, &end);
  } catch (std::exception& e) {
    throw Error(e.what());
  }
  if (end != value.size()) {
    throw Error("invalid floating point: \"{}\"", value);
  }
  return result;
}

std::string
format_cache_size(uint64_t value)
{
  return Util::format_parsable_size_with_suffix(value);
}

CompilerType
parse_compiler_type(const std::string& value)
{
  if (value == "clang") {
    return CompilerType::clang;
  } else if (value == "gcc") {
    return CompilerType::gcc;
  } else if (value == "nvcc") {
    return CompilerType::nvcc;
  } else if (value == "other") {
    return CompilerType::other;
  } else if (value == "pump") {
    return CompilerType::pump;
  } else {
    // Allow any unknown value for forward compatibility.
    return CompilerType::auto_guess;
  }
}

uint32_t
parse_sloppiness(const std::string& value)
{
  size_t start = 0;
  size_t end = 0;
  uint32_t result = 0;
  while (end != std::string::npos) {
    end = value.find_first_of(", ", start);
    std::string token =
      Util::strip_whitespace(value.substr(start, end - start));
    if (token == "file_stat_matches") {
      result |= SLOPPY_FILE_STAT_MATCHES;
    } else if (token == "file_stat_matches_ctime") {
      result |= SLOPPY_FILE_STAT_MATCHES_CTIME;
    } else if (token == "include_file_ctime") {
      result |= SLOPPY_INCLUDE_FILE_CTIME;
    } else if (token == "include_file_mtime") {
      result |= SLOPPY_INCLUDE_FILE_MTIME;
    } else if (token == "system_headers" || token == "no_system_headers") {
      result |= SLOPPY_SYSTEM_HEADERS;
    } else if (token == "pch_defines") {
      result |= SLOPPY_PCH_DEFINES;
    } else if (token == "time_macros") {
      result |= SLOPPY_TIME_MACROS;
    } else if (token == "clang_index_store") {
      result |= SLOPPY_CLANG_INDEX_STORE;
    } else if (token == "locale") {
      result |= SLOPPY_LOCALE;
    } else if (token == "modules") {
      result |= SLOPPY_MODULES;
    } else if (token == "ivfsoverlay") {
      result |= SLOPPY_IVFSOVERLAY;
    } // else: ignore unknown value for forward compatibility
    start = value.find_first_not_of(", ", end);
  }
  return result;
}

std::string
format_sloppiness(uint32_t sloppiness)
{
  std::string result;
  if (sloppiness & SLOPPY_INCLUDE_FILE_MTIME) {
    result += "include_file_mtime, ";
  }
  if (sloppiness & SLOPPY_INCLUDE_FILE_CTIME) {
    result += "include_file_ctime, ";
  }
  if (sloppiness & SLOPPY_TIME_MACROS) {
    result += "time_macros, ";
  }
  if (sloppiness & SLOPPY_PCH_DEFINES) {
    result += "pch_defines, ";
  }
  if (sloppiness & SLOPPY_FILE_STAT_MATCHES) {
    result += "file_stat_matches, ";
  }
  if (sloppiness & SLOPPY_FILE_STAT_MATCHES_CTIME) {
    result += "file_stat_matches_ctime, ";
  }
  if (sloppiness & SLOPPY_SYSTEM_HEADERS) {
    result += "system_headers, ";
  }
  if (sloppiness & SLOPPY_CLANG_INDEX_STORE) {
    result += "clang_index_store, ";
  }
  if (sloppiness & SLOPPY_LOCALE) {
    result += "locale, ";
  }
  if (sloppiness & SLOPPY_MODULES) {
    result += "modules, ";
  }
  if (sloppiness & SLOPPY_IVFSOVERLAY) {
    result += "ivfsoverlay, ";
  }
  if (!result.empty()) {
    // Strip last ", ".
    result.resize(result.size() - 2);
  }
  return result;
}

uint32_t
parse_umask(const std::string& value)
{
  if (value.empty()) {
    return std::numeric_limits<uint32_t>::max();
  }

  size_t end;
  uint32_t result = std::stoul(value, &end, 8);
  if (end != value.size()) {
    throw Error("not an octal integer: \"{}\"", value);
  }
  return result;
}

std::string
format_umask(uint32_t umask)
{
  if (umask == std::numeric_limits<uint32_t>::max()) {
    return {};
  } else {
    return FMT("{:03o}", umask);
  }
}

void
verify_absolute_path(const std::string& value)
{
  if (!Util::is_absolute_path(value)) {
    throw Error("not an absolute path: \"{}\"", value);
  }
}

bool
parse_line(const std::string& line,
           std::string* key,
           std::string* value,
           std::string* error_message)
{
  std::string stripped_line = Util::strip_whitespace(line);
  if (stripped_line.empty() || stripped_line[0] == '#') {
    return true;
  }
  size_t equal_pos = stripped_line.find('=');
  if (equal_pos == std::string::npos) {
    *error_message = "missing equal sign";
    return false;
  }
  *key = stripped_line.substr(0, equal_pos);
  *value = stripped_line.substr(equal_pos + 1);
  *key = Util::strip_whitespace(*key);
  *value = Util::strip_whitespace(*value);
  return true;
}

// `line` is the full configuration line excluding newline. `key` will be empty
// for comments and blank lines. `value` does not include newline.
using ConfigLineHandler = std::function<void(
  const std::string& line, const std::string& key, const std::string& value)>;

// Call `config_line_handler` for each line in `path`.
bool
parse_config_file(const std::string& path,
                  const ConfigLineHandler& config_line_handler)
{
  std::ifstream file(path);
  if (!file) {
    return false;
  }

  std::string line;

  size_t line_number = 0;
  while (std::getline(file, line)) {
    ++line_number;

    try {
      std::string key;
      std::string value;
      std::string error_message;
      if (!parse_line(line, &key, &value, &error_message)) {
        throw Error(error_message);
      }
      config_line_handler(line, key, value);
    } catch (const Error& e) {
      throw Error("{}:{}: {}", path, line_number, e.what());
    }
  }
  return true;
}

} // namespace

std::string
compiler_type_to_string(CompilerType compiler_type)
{
#define CASE(type)                                                             \
  case CompilerType::type:                                                     \
    return #type

  switch (compiler_type) {
  case CompilerType::auto_guess:
    return "auto";

    CASE(clang);
    CASE(gcc);
    CASE(nvcc);
    CASE(other);
    CASE(pump);
  }
#undef CASE

  ASSERT(false);
}

const std::string&
Config::primary_config_path() const
{
  return m_primary_config_path;
}

const std::string&
Config::secondary_config_path() const
{
  return m_secondary_config_path;
}

void
Config::set_primary_config_path(std::string path)
{
  m_primary_config_path = std::move(path);
}

void
Config::set_secondary_config_path(std::string path)
{
  m_secondary_config_path = std::move(path);
}

bool
Config::update_from_file(const std::string& path)
{
  return parse_config_file(path,
                           [&](const std::string& /*line*/,
                               const std::string& key,
                               const std::string& value) {
                             if (!key.empty()) {
                               set_item(key, value, nullopt, false, path);
                             }
                           });
}

void
Config::update_from_environment()
{
  for (char** env = environ; *env; ++env) {
    std::string setting = *env;
    const std::string prefix = "CCACHE_";
    if (!Util::starts_with(setting, prefix)) {
      continue;
    }
    size_t equal_pos = setting.find('=');
    if (equal_pos == std::string::npos) {
      continue;
    }

    std::string key = setting.substr(prefix.size(), equal_pos - prefix.size());
    std::string value = setting.substr(equal_pos + 1);
    bool negate = Util::starts_with(key, "NO");
    if (negate) {
      key = key.substr(2);
    }

    auto it = k_env_variable_table.find(key);
    if (it == k_env_variable_table.end()) {
      // Ignore unknown keys.
      continue;
    }
    const auto& config_key = it->second;

    try {
      set_item(config_key, value, key, negate, "environment");
    } catch (const Error& e) {
      throw Error("CCACHE_{}{}: {}", negate ? "NO" : "", key, e.what());
    }
  }
}

std::string
Config::get_string_value(const std::string& key) const
{
  auto it = k_config_key_table.find(key);
  if (it == k_config_key_table.end()) {
    throw Error("unknown configuration option \"{}\"", key);
  }

  switch (it->second) {
  case ConfigItem::absolute_paths_in_stderr:
    return format_bool(m_absolute_paths_in_stderr);

  case ConfigItem::base_dir:
    return m_base_dir;

  case ConfigItem::cache_dir:
    return m_cache_dir;

  case ConfigItem::compiler:
    return m_compiler;

  case ConfigItem::compiler_check:
    return m_compiler_check;

  case ConfigItem::compiler_type:
    return compiler_type_to_string(m_compiler_type);

  case ConfigItem::compression:
    return format_bool(m_compression);

  case ConfigItem::compression_level:
    return FMT("{}", m_compression_level);

  case ConfigItem::cpp_extension:
    return m_cpp_extension;

  case ConfigItem::debug:
    return format_bool(m_debug);

  case ConfigItem::debug_dir:
    return m_debug_dir;

  case ConfigItem::depend_mode:
    return format_bool(m_depend_mode);

  case ConfigItem::direct_mode:
    return format_bool(m_direct_mode);

  case ConfigItem::disable:
    return format_bool(m_disable);

  case ConfigItem::extra_files_to_hash:
    return m_extra_files_to_hash;

  case ConfigItem::file_clone:
    return format_bool(m_file_clone);

  case ConfigItem::hard_link:
    return format_bool(m_hard_link);

  case ConfigItem::hash_dir:
    return format_bool(m_hash_dir);

  case ConfigItem::ignore_headers_in_manifest:
    return m_ignore_headers_in_manifest;

  case ConfigItem::ignore_options:
    return m_ignore_options;

  case ConfigItem::inode_cache:
    return format_bool(m_inode_cache);

  case ConfigItem::keep_comments_cpp:
    return format_bool(m_keep_comments_cpp);

  case ConfigItem::limit_multiple:
    return FMT("{:.1f}", m_limit_multiple);

  case ConfigItem::log_file:
    return m_log_file;

  case ConfigItem::max_files:
    return FMT("{}", m_max_files);

  case ConfigItem::max_size:
    return format_cache_size(m_max_size);

  case ConfigItem::path:
    return m_path;

  case ConfigItem::pch_external_checksum:
    return format_bool(m_pch_external_checksum);

  case ConfigItem::prefix_command:
    return m_prefix_command;

  case ConfigItem::prefix_command_cpp:
    return m_prefix_command_cpp;

  case ConfigItem::read_only:
    return format_bool(m_read_only);

  case ConfigItem::read_only_direct:
    return format_bool(m_read_only_direct);

  case ConfigItem::recache:
    return format_bool(m_recache);

  case ConfigItem::run_second_cpp:
    return format_bool(m_run_second_cpp);

  case ConfigItem::sloppiness:
    return format_sloppiness(m_sloppiness);

  case ConfigItem::stats:
    return format_bool(m_stats);

  case ConfigItem::temporary_dir:
    return m_temporary_dir;

  case ConfigItem::umask:
    return format_umask(m_umask);
  }

  ASSERT(false); // Never reached
}

void
Config::set_value_in_file(const std::string& path,
                          const std::string& key,
                          const std::string& value)
{
  if (k_config_key_table.find(key) == k_config_key_table.end()) {
    throw Error("unknown configuration option \"{}\"", key);
  }

  // Verify that the value is valid; set_item will throw if not.
  Config dummy_config;
  dummy_config.set_item(key, value, nullopt, false, "");

  const auto resolved_path = Util::real_path(path);
  const auto st = Stat::stat(resolved_path);
  if (!st) {
    Util::ensure_dir_exists(Util::dir_name(resolved_path));
    try {
      Util::write_file(resolved_path, "");
    } catch (const Error& e) {
      throw Error("failed to write to {}: {}", resolved_path, e.what());
    }
  }

  AtomicFile output(resolved_path, AtomicFile::Mode::text);
  bool found = false;

  if (!parse_config_file(path,
                         [&](const std::string& c_line,
                             const std::string& c_key,
                             const std::string& /*c_value*/) {
                           if (c_key == key) {
                             output.write(FMT("{} = {}\n", key, value));
                             found = true;
                           } else {
                             output.write(FMT("{}\n", c_line));
                           }
                         })) {
    throw Error("failed to open {}: {}", path, strerror(errno));
  }

  if (!found) {
    output.write(FMT("{} = {}\n", key, value));
  }

  output.commit();
}

void
Config::visit_items(const ItemVisitor& item_visitor) const
{
  std::vector<std::string> keys;
  keys.reserve(k_config_key_table.size());

  for (const auto& item : k_config_key_table) {
    keys.emplace_back(item.first);
  }
  std::sort(keys.begin(), keys.end());
  for (const auto& key : keys) {
    auto it = m_origins.find(key);
    std::string origin = it != m_origins.end() ? it->second : "default";
    item_visitor(key, get_string_value(key), origin);
  }
}

void
Config::set_item(const std::string& key,
                 const std::string& value,
                 const optional<std::string>& env_var_key,
                 bool negate,
                 const std::string& origin)
{
  auto it = k_config_key_table.find(key);
  if (it == k_config_key_table.end()) {
    // Ignore unknown keys.
    return;
  }

  switch (it->second) {
  case ConfigItem::absolute_paths_in_stderr:
    m_absolute_paths_in_stderr = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::base_dir:
    m_base_dir = Util::expand_environment_variables(value);
    if (!m_base_dir.empty()) { // The empty string means "disable"
      verify_absolute_path(m_base_dir);
      m_base_dir = Util::normalize_absolute_path(m_base_dir);
    }
    break;

  case ConfigItem::cache_dir:
    set_cache_dir(Util::expand_environment_variables(value));
    break;

  case ConfigItem::compiler:
    m_compiler = value;
    break;

  case ConfigItem::compiler_check:
    m_compiler_check = value;
    break;

  case ConfigItem::compiler_type:
    m_compiler_type = parse_compiler_type(value);
    break;

  case ConfigItem::compression:
    m_compression = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::compression_level: {
    m_compression_level =
      Util::parse_signed(value, INT8_MIN, INT8_MAX, "compression_level");
    break;
  }

  case ConfigItem::cpp_extension:
    m_cpp_extension = value;
    break;

  case ConfigItem::debug:
    m_debug = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::debug_dir:
    m_debug_dir = value;
    break;

  case ConfigItem::depend_mode:
    m_depend_mode = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::direct_mode:
    m_direct_mode = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::disable:
    m_disable = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::extra_files_to_hash:
    m_extra_files_to_hash = Util::expand_environment_variables(value);
    break;

  case ConfigItem::file_clone:
    m_file_clone = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::hard_link:
    m_hard_link = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::hash_dir:
    m_hash_dir = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::ignore_headers_in_manifest:
    m_ignore_headers_in_manifest = Util::expand_environment_variables(value);
    break;

  case ConfigItem::ignore_options:
    m_ignore_options = Util::expand_environment_variables(value);
    break;

  case ConfigItem::inode_cache:
    m_inode_cache = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::keep_comments_cpp:
    m_keep_comments_cpp = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::limit_multiple:
    m_limit_multiple = Util::clamp(parse_double(value), 0.0, 1.0);
    break;

  case ConfigItem::log_file:
    m_log_file = Util::expand_environment_variables(value);
    break;

  case ConfigItem::max_files:
    m_max_files = Util::parse_unsigned(value, nullopt, nullopt, "max_files");
    break;

  case ConfigItem::max_size:
    m_max_size = Util::parse_size(value);
    break;

  case ConfigItem::path:
    m_path = Util::expand_environment_variables(value);
    break;

  case ConfigItem::pch_external_checksum:
    m_pch_external_checksum = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::prefix_command:
    m_prefix_command = Util::expand_environment_variables(value);
    break;

  case ConfigItem::prefix_command_cpp:
    m_prefix_command_cpp = Util::expand_environment_variables(value);
    break;

  case ConfigItem::read_only:
    m_read_only = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::read_only_direct:
    m_read_only_direct = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::recache:
    m_recache = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::run_second_cpp:
    m_run_second_cpp = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::sloppiness:
    m_sloppiness = parse_sloppiness(value);
    break;

  case ConfigItem::stats:
    m_stats = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::temporary_dir:
    m_temporary_dir = Util::expand_environment_variables(value);
    m_temporary_dir_configured_explicitly = true;
    break;

  case ConfigItem::umask:
    m_umask = parse_umask(value);
    break;
  }

  m_origins.emplace(key, origin);
}

void
Config::check_key_tables_consistency()
{
  for (const auto& item : k_env_variable_table) {
    if (k_config_key_table.find(item.second) == k_config_key_table.end()) {
      throw Error(
        "env var {} mapped to {} which is missing from k_config_key_table",
        item.first,
        item.second);
    }
  }
}

std::string
Config::default_temporary_dir(const std::string& cache_dir)
{
#ifdef HAVE_GETEUID
  std::string user_tmp_dir = FMT("/run/user/{}", geteuid());
  if (Stat::stat(user_tmp_dir).is_directory()) {
    return user_tmp_dir + "/ccache-tmp";
  }
#endif
  return cache_dir + "/tmp";
}
