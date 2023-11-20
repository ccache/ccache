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

#include "Config.hpp"

#include "MiniTrace.hpp"
#include "Util.hpp"

#include <core/AtomicFile.hpp>
#include <core/common.hpp>
#include <core/exceptions.hpp>
#include <core/types.hpp>
#include <util/DirEntry.hpp>
#include <util/Tokenizer.hpp>
#include <util/UmaskScope.hpp>
#include <util/assertions.hpp>
#include <util/environment.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/fmtmacros.hpp>
#include <util/path.hpp>
#include <util/string.hpp>
#include <util/wincompat.hpp>

#include "third_party/fmt/core.h"

#include <sys/types.h>

#ifdef HAVE_PWD_H
#  include <pwd.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>
#include <cassert>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef environ
DLLIMPORT extern char** environ;
#endif

// Make room for binary patching at install time.
const char k_sysconfdir[4096 + 1] = SYSCONFDIR;

namespace fs = util::filesystem;

using util::DirEntry;

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
  debug_level,
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
  log_file,
  max_files,
  max_size,
  msvc_dep_prefix,
  namespace_,
  path,
  pch_external_checksum,
  prefix_command,
  prefix_command_cpp,
  read_only,
  read_only_direct,
  recache,
  remote_only,
  remote_storage,
  reshare,
  run_second_cpp,
  sloppiness,
  stats,
  stats_log,
  temporary_dir,
  umask,
};

enum class ConfigKeyType { normal, alias };

struct ConfigKeyTableEntry
{
  ConfigItem item;
  std::optional<std::string> alias = std::nullopt;
};

const std::unordered_map<std::string, ConfigKeyTableEntry> k_config_key_table =
  {
    {"absolute_paths_in_stderr", {ConfigItem::absolute_paths_in_stderr}},
    {"base_dir", {ConfigItem::base_dir}},
    {"cache_dir", {ConfigItem::cache_dir}},
    {"compiler", {ConfigItem::compiler}},
    {"compiler_check", {ConfigItem::compiler_check}},
    {"compiler_type", {ConfigItem::compiler_type}},
    {"compression", {ConfigItem::compression}},
    {"compression_level", {ConfigItem::compression_level}},
    {"cpp_extension", {ConfigItem::cpp_extension}},
    {"debug", {ConfigItem::debug}},
    {"debug_dir", {ConfigItem::debug_dir}},
    {"debug_level", {ConfigItem::debug_level}},
    {"depend_mode", {ConfigItem::depend_mode}},
    {"direct_mode", {ConfigItem::direct_mode}},
    {"disable", {ConfigItem::disable}},
    {"extra_files_to_hash", {ConfigItem::extra_files_to_hash}},
    {"file_clone", {ConfigItem::file_clone}},
    {"hard_link", {ConfigItem::hard_link}},
    {"hash_dir", {ConfigItem::hash_dir}},
    {"ignore_headers_in_manifest", {ConfigItem::ignore_headers_in_manifest}},
    {"ignore_options", {ConfigItem::ignore_options}},
    {"inode_cache", {ConfigItem::inode_cache}},
    {"keep_comments_cpp", {ConfigItem::keep_comments_cpp}},
    {"log_file", {ConfigItem::log_file}},
    {"max_files", {ConfigItem::max_files}},
    {"max_size", {ConfigItem::max_size}},
    {"msvc_dep_prefix", {ConfigItem::msvc_dep_prefix}},
    {"namespace", {ConfigItem::namespace_}},
    {"path", {ConfigItem::path}},
    {"pch_external_checksum", {ConfigItem::pch_external_checksum}},
    {"prefix_command", {ConfigItem::prefix_command}},
    {"prefix_command_cpp", {ConfigItem::prefix_command_cpp}},
    {"read_only", {ConfigItem::read_only}},
    {"read_only_direct", {ConfigItem::read_only_direct}},
    {"recache", {ConfigItem::recache}},
    {"remote_only", {ConfigItem::remote_only}},
    {"remote_storage", {ConfigItem::remote_storage}},
    {"reshare", {ConfigItem::reshare}},
    {"run_second_cpp", {ConfigItem::run_second_cpp}},
    {"secondary_storage", {ConfigItem::remote_storage, "remote_storage"}},
    {"sloppiness", {ConfigItem::sloppiness}},
    {"stats", {ConfigItem::stats}},
    {"stats_log", {ConfigItem::stats_log}},
    {"temporary_dir", {ConfigItem::temporary_dir}},
    {"umask", {ConfigItem::umask}},
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
  {"DEBUGLEVEL", "debug_level"},
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
  {"LOGFILE", "log_file"},
  {"MAXFILES", "max_files"},
  {"MAXSIZE", "max_size"},
  {"MSVC_DEP_PREFIX", "msvc_dep_prefix"},
  {"NAMESPACE", "namespace"},
  {"PATH", "path"},
  {"PCH_EXTSUM", "pch_external_checksum"},
  {"PREFIX", "prefix_command"},
  {"PREFIX_CPP", "prefix_command_cpp"},
  {"READONLY", "read_only"},
  {"READONLY_DIRECT", "read_only_direct"},
  {"RECACHE", "recache"},
  {"REMOTE_ONLY", "remote_only"},
  {"REMOTE_STORAGE", "remote_storage"},
  {"RESHARE", "reshare"},
  {"SECONDARY_STORAGE", "remote_storage"}, // Alias for CCACHE_REMOTE_STORAGE
  {"SLOPPINESS", "sloppiness"},
  {"STATS", "stats"},
  {"STATSLOG", "stats_log"},
  {"TEMPDIR", "temporary_dir"},
  {"UMASK", "umask"},
};

bool
parse_bool(const std::string& value,
           const std::optional<std::string> env_var_key,
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
    std::string lower_value = util::to_lowercase(value);
    if (value == "0" || lower_value == "false" || lower_value == "disable"
        || lower_value == "no") {
      throw core::Error(
        FMT("invalid boolean environment variable value \"{}\" (did you mean to"
            " set \"CCACHE_{}{}=true\"?)",
            value,
            negate ? "" : "NO",
            *env_var_key));
    }
    return !negate;
  } else if (value == "true") {
    return true;
  } else if (value == "false") {
    return false;
  } else {
    throw core::Error(FMT("not a boolean value: \"{}\"", value));
  }
}

std::string
format_bool(bool value)
{
  return value ? "true" : "false";
}

CompilerType
parse_compiler_type(const std::string& value)
{
  if (value == "clang") {
    return CompilerType::clang;
  } else if (value == "clang-cl") {
    return CompilerType::clang_cl;
  } else if (value == "gcc") {
    return CompilerType::gcc;
  } else if (value == "icl") {
    return CompilerType::icl;
  } else if (value == "msvc") {
    return CompilerType::msvc;
  } else if (value == "nvcc") {
    return CompilerType::nvcc;
  } else if (value == "other") {
    return CompilerType::other;
  } else {
    // Allow any unknown value for forward compatibility.
    return CompilerType::auto_guess;
  }
}

core::Sloppiness
parse_sloppiness(const std::string& value)
{
  core::Sloppiness result;

  for (const auto token : util::Tokenizer(value, ", ")) {
    if (token == "clang_index_store") {
      result.insert(core::Sloppy::clang_index_store);
    } else if (token == "file_stat_matches") {
      result.insert(core::Sloppy::file_stat_matches);
    } else if (token == "file_stat_matches_ctime") {
      result.insert(core::Sloppy::file_stat_matches_ctime);
    } else if (token == "gcno_cwd") {
      result.insert(core::Sloppy::gcno_cwd);
    } else if (token == "include_file_ctime") {
      result.insert(core::Sloppy::include_file_ctime);
    } else if (token == "include_file_mtime") {
      result.insert(core::Sloppy::include_file_mtime);
    } else if (token == "ivfsoverlay") {
      result.insert(core::Sloppy::ivfsoverlay);
    } else if (token == "locale") {
      result.insert(core::Sloppy::locale);
    } else if (token == "modules") {
      result.insert(core::Sloppy::modules);
    } else if (token == "pch_defines") {
      result.insert(core::Sloppy::pch_defines);
    } else if (token == "random_seed") {
      result.insert(core::Sloppy::random_seed);
    } else if (token == "system_headers" || token == "no_system_headers") {
      result.insert(core::Sloppy::system_headers);
    } else if (token == "time_macros") {
      result.insert(core::Sloppy::time_macros);
    } // else: ignore unknown value for forward compatibility
  }

  return result;
}

std::string
format_sloppiness(core::Sloppiness sloppiness)
{
  std::string result;
  if (sloppiness.contains(core::Sloppy::clang_index_store)) {
    result += "clang_index_store, ";
  }
  if (sloppiness.contains(core::Sloppy::file_stat_matches)) {
    result += "file_stat_matches, ";
  }
  if (sloppiness.contains(core::Sloppy::file_stat_matches_ctime)) {
    result += "file_stat_matches_ctime, ";
  }
  if (sloppiness.contains(core::Sloppy::gcno_cwd)) {
    result += "gcno_cwd, ";
  }
  if (sloppiness.contains(core::Sloppy::include_file_ctime)) {
    result += "include_file_ctime, ";
  }
  if (sloppiness.contains(core::Sloppy::include_file_mtime)) {
    result += "include_file_mtime, ";
  }
  if (sloppiness.contains(core::Sloppy::ivfsoverlay)) {
    result += "ivfsoverlay, ";
  }
  if (sloppiness.contains(core::Sloppy::locale)) {
    result += "locale, ";
  }
  if (sloppiness.contains(core::Sloppy::modules)) {
    result += "modules, ";
  }
  if (sloppiness.contains(core::Sloppy::pch_defines)) {
    result += "pch_defines, ";
  }
  if (sloppiness.contains(core::Sloppy::random_seed)) {
    result += "random_seed, ";
  }
  if (sloppiness.contains(core::Sloppy::system_headers)) {
    result += "system_headers, ";
  }
  if (sloppiness.contains(core::Sloppy::time_macros)) {
    result += "time_macros, ";
  }
  if (!result.empty()) {
    // Strip last ", ".
    result.resize(result.size() - 2);
  }
  return result;
}

std::string
format_umask(std::optional<mode_t> umask)
{
  if (umask) {
    return FMT("{:03o}", *umask);
  } else {
    return {};
  }
}

void
verify_absolute_path(const std::string& value)
{
  if (!util::is_absolute_path(value)) {
    throw core::Error(FMT("not an absolute path: \"{}\"", value));
  }
}

bool
parse_line(const std::string& line,
           std::string* key,
           std::string* value,
           std::string* error_message)
{
  std::string stripped_line = util::strip_whitespace(line);
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
  *key = util::strip_whitespace(*key);
  *value = util::strip_whitespace(*value);
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
        throw core::Error(error_message);
      }
      config_line_handler(line, key, value);
    } catch (const core::Error& e) {
      throw core::Error(FMT("{}:{}: {}", path, line_number, e.what()));
    }
  }
  return true;
}

std::unordered_map<std::string, std::string>
create_cmdline_settings_map(const std::vector<std::string>& settings)
{
  std::unordered_map<std::string, std::string> result;
  for (const auto& setting : settings) {
    DEBUG_ASSERT(setting.find('=') != std::string::npos);
    std::string key;
    std::string value;
    std::string error_message;
    bool ok = parse_line(setting, &key, &value, &error_message);
    ASSERT(ok);
    if (!key.empty()) {
      result.insert_or_assign(std::move(key), std::move(value));
    }
  }
  return result;
}

} // namespace

#ifndef _WIN32
static std::string
default_cache_dir(const std::string& home_dir)
{
#  ifdef __APPLE__
  return home_dir + "/Library/Caches/ccache";
#  else
  return home_dir + "/.cache/ccache";
#  endif
}

static std::string
default_config_dir(const std::string& home_dir)
{
#  ifdef __APPLE__
  return home_dir + "/Library/Preferences/ccache";
#  else
  return home_dir + "/.config/ccache";
#  endif
}
#endif

std::string
home_directory()
{
#ifdef _WIN32
  if (const char* p = getenv("USERPROFILE")) {
    return p;
  }
  throw core::Fatal(
    "The USERPROFILE environment variable must be set to your user profile"
    " folder");
#else
  if (const char* p = getenv("HOME")) {
    return p;
  }
#  ifdef HAVE_GETPWUID
  {
    struct passwd* pwd = getpwuid(getuid());
    if (pwd) {
      return pwd->pw_dir;
    }
  }
#  endif
  throw core::Fatal(
    "Could not determine home directory from $HOME or getpwuid(3)");
#endif
}

std::string
compiler_type_to_string(CompilerType compiler_type)
{
#define CASE(type)                                                             \
  case CompilerType::type:                                                     \
    return #type

  switch (compiler_type) {
  case CompilerType::auto_guess:
    return "auto";
  case CompilerType::clang_cl:
    return "clang-cl";

    CASE(clang);
    CASE(gcc);
    CASE(icl);
    CASE(msvc);
    CASE(nvcc);
    CASE(ti);
    CASE(other);
  }
#undef CASE

  ASSERT(false);
}

void
Config::read(const std::vector<std::string>& cmdline_config_settings)
{
  auto cmdline_settings_map =
    create_cmdline_settings_map(cmdline_config_settings);

  const std::string home_dir = home_directory();
  const std::string legacy_ccache_dir = Util::make_path(home_dir, ".ccache");
  const bool legacy_ccache_dir_exists =
    DirEntry(legacy_ccache_dir).is_directory();
#ifdef _WIN32
  const char* const env_appdata = getenv("APPDATA");
  const char* const env_local_appdata = getenv("LOCALAPPDATA");
#else
  const char* const env_xdg_cache_home = getenv("XDG_CACHE_HOME");
  const char* const env_xdg_config_home = getenv("XDG_CONFIG_HOME");
#endif

  const char* env_ccache_configpath = getenv("CCACHE_CONFIGPATH");
  if (env_ccache_configpath) {
    set_config_path(env_ccache_configpath);
  } else {
    // Only used for ccache tests:
    const char* const env_ccache_configpath2 = getenv("CCACHE_CONFIGPATH2");

    std::string sysconfdir = Util::make_path(k_sysconfdir);
#ifdef _WIN32
    if (const char* program_data = getenv("ALLUSERSPROFILE"))
      sysconfdir = Util::make_path(program_data, "ccache");
#endif

    set_system_config_path(env_ccache_configpath2
                             ? env_ccache_configpath2
                             : Util::make_path(sysconfdir, "ccache.conf"));
    MTR_BEGIN("config", "conf_read_system");
    // A missing config file in SYSCONFDIR is OK so don't check return value.
    update_from_file(system_config_path());
    MTR_END("config", "conf_read_system");

    const char* const env_ccache_dir = getenv("CCACHE_DIR");
    auto cmdline_cache_dir = cmdline_settings_map.find("cache_dir");

    std::string config_dir;
    if (cmdline_cache_dir != cmdline_settings_map.end()) {
      config_dir = cmdline_cache_dir->second;
    } else if (env_ccache_dir && *env_ccache_dir) {
      config_dir = env_ccache_dir;
    } else if (!cache_dir().empty() && !env_ccache_dir) {
      config_dir = cache_dir();
    } else if (legacy_ccache_dir_exists) {
      config_dir = legacy_ccache_dir;
#ifdef _WIN32
    } else if (env_local_appdata
               && DirEntry(
                 Util::make_path(env_local_appdata, "ccache", "ccache.conf"))) {
      config_dir = Util::make_path(env_local_appdata, "ccache");
    } else if (env_appdata
               && DirEntry(
                 Util::make_path(env_appdata, "ccache", "ccache.conf"))) {
      config_dir = Util::make_path(env_appdata, "ccache");
    } else if (env_local_appdata) {
      config_dir = Util::make_path(env_local_appdata, "ccache");
    } else {
      throw core::Fatal(
        "could not find configuration file and the LOCALAPPDATA environment"
        " variable is not set");
    }
#else
    } else if (env_xdg_config_home) {
      config_dir = Util::make_path(env_xdg_config_home, "ccache");
    } else {
      config_dir = default_config_dir(home_dir);
    }
#endif
    set_config_path(Util::make_path(config_dir, "ccache.conf"));
  }

  const std::string& cache_dir_before_config_file_was_read = cache_dir();

  MTR_BEGIN("config", "conf_read");
  update_from_file(config_path());
  MTR_END("config", "conf_read");

  // Ignore cache_dir set in configuration file
  set_cache_dir(cache_dir_before_config_file_was_read);

  MTR_BEGIN("config", "conf_update_from_environment");
  update_from_environment();
  // (cache_dir is set above if CCACHE_DIR is set.)
  MTR_END("config", "conf_update_from_environment");

  update_from_map(cmdline_settings_map);

  if (cache_dir().empty()) {
    if (legacy_ccache_dir_exists) {
      set_cache_dir(legacy_ccache_dir);
#ifdef _WIN32
    } else if (env_local_appdata) {
      set_cache_dir(Util::make_path(env_local_appdata, "ccache"));
    } else {
      throw core::Fatal(
        "could not find cache directory and the LOCALAPPDATA environment"
        " variable is not set");
    }
#else
    } else if (env_xdg_cache_home) {
      set_cache_dir(Util::make_path(env_xdg_cache_home, "ccache"));
    } else {
      set_cache_dir(default_cache_dir(home_dir));
    }
#endif
  }
  // else: cache_dir was set explicitly via environment or via system config.

  // We have now determined config.cache_dir and populated the rest of config in
  // prio order (1. command line, 2. environment, 3. cache-specific config, 4.
  // system config).
}

const std::string&
Config::config_path() const
{
  return m_config_path;
}

const std::string&
Config::system_config_path() const
{
  return m_system_config_path;
}

void
Config::set_config_path(std::string path)
{
  m_config_path = std::move(path);
}

void
Config::set_system_config_path(std::string path)
{
  m_system_config_path = std::move(path);
}

bool
Config::update_from_file(const std::string& path)
{
  return parse_config_file(
    path, [&](const auto& /*line*/, const auto& key, const auto& value) {
      if (!key.empty()) {
        set_item(key, value, std::nullopt, false, path);
      }
    });
}

void
Config::update_from_map(const std::unordered_map<std::string, std::string>& map)
{
  for (const auto& [key, value] : map) {
    try {
      set_item(key, value, std::nullopt, false, "command line");
    } catch (core::Error& e) {
      throw core::Error(
        FMT("when parsing command line config \"{}\": {}", key, e.what()));
    }
  }
}

void
Config::update_from_environment()
{
  for (char** env = environ; *env; ++env) {
    std::string setting = *env;
    const std::string prefix = "CCACHE_";
    if (!util::starts_with(setting, prefix)) {
      continue;
    }
    size_t equal_pos = setting.find('=');
    if (equal_pos == std::string::npos) {
      continue;
    }

    std::string key = setting.substr(prefix.size(), equal_pos - prefix.size());
    std::string value = setting.substr(equal_pos + 1);
    bool negate = util::starts_with(key, "NO");
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
    } catch (const core::Error& e) {
      throw core::Error(
        FMT("CCACHE_{}{}: {}", negate ? "NO" : "", key, e.what()));
    }
  }
}

std::string
Config::get_string_value(const std::string& key) const
{
  auto it = k_config_key_table.find(key);
  if (it == k_config_key_table.end()) {
    throw core::Error(FMT("unknown configuration option \"{}\"", key));
  }

  switch (it->second.item) {
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
    return m_debug_dir.string();

  case ConfigItem::debug_level:
    return FMT("{}", m_debug_level);

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

  case ConfigItem::log_file:
    return m_log_file;

  case ConfigItem::max_files:
    return FMT("{}", m_max_files);

  case ConfigItem::max_size: {
    auto result =
      util::format_human_readable_size(m_max_size, m_size_prefix_type);
    if (util::ends_with(result, " bytes")) {
      // Special case to make the output parsable by util::parse_size.
      result.resize(result.size() - 6);
    }
    return result;
  }

  case ConfigItem::msvc_dep_prefix:
    return m_msvc_dep_prefix;

  case ConfigItem::namespace_:
    return m_namespace;

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

  case ConfigItem::remote_only:
    return format_bool(m_remote_only);

  case ConfigItem::remote_storage:
    return m_remote_storage;

  case ConfigItem::reshare:
    return format_bool(m_reshare);

  case ConfigItem::run_second_cpp:
    return format_bool(m_run_second_cpp);

  case ConfigItem::sloppiness:
    return format_sloppiness(m_sloppiness);

  case ConfigItem::stats:
    return format_bool(m_stats);

  case ConfigItem::stats_log:
    return m_stats_log;

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
                          const std::string& value) const
{
  util::UmaskScope umask_scope(m_umask);

  if (k_config_key_table.find(key) == k_config_key_table.end()) {
    throw core::Error(FMT("unknown configuration option \"{}\"", key));
  }

  // Verify that the value is valid; set_item will throw if not.
  Config dummy_config;
  dummy_config.set_item(key, value, std::nullopt, false, "");

  const auto resolved_path = util::real_path(path);
  if (!DirEntry(resolved_path).is_regular_file()) {
    core::ensure_dir_exists(Util::dir_name(resolved_path));
    util::throw_on_error<core::Error>(
      util::write_file(resolved_path, ""),
      FMT("failed to write to {}: ", resolved_path));
  }

  core::AtomicFile output(resolved_path, core::AtomicFile::Mode::text);
  bool found = false;

  if (!parse_config_file(
        path,
        [&](const auto& c_line, const auto& c_key, const auto& /*c_value*/) {
          if (c_key == key) {
            output.write(FMT("{} = {}\n", key, value));
            found = true;
          } else {
            output.write(FMT("{}\n", c_line));
          }
        })) {
    throw core::Error(FMT("failed to open {}: {}", path, strerror(errno)));
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

  for (const auto& [key, entry] : k_config_key_table) {
    if (!entry.alias) {
      keys.emplace_back(key);
    }
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
                 const std::string& unexpanded_value,
                 const std::optional<std::string>& env_var_key,
                 bool negate,
                 const std::string& origin)
{
  auto it = k_config_key_table.find(key);
  if (it == k_config_key_table.end()) {
    // Ignore unknown keys.
    return;
  }

  std::string value = util::value_or_throw<core::Error>(
    util::expand_environment_variables(unexpanded_value));

  switch (it->second.item) {
  case ConfigItem::absolute_paths_in_stderr:
    m_absolute_paths_in_stderr = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::base_dir:
    m_base_dir = value;
    if (!m_base_dir.empty()) { // The empty string means "disable"
      verify_absolute_path(m_base_dir);
      m_base_dir = Util::normalize_abstract_absolute_path(m_base_dir);
    }
    break;

  case ConfigItem::cache_dir:
    set_cache_dir(value);
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

  case ConfigItem::compression_level:
    m_compression_level = util::value_or_throw<core::Error>(
      util::parse_signed(value, INT8_MIN, INT8_MAX, "compression_level"));
    break;

  case ConfigItem::cpp_extension:
    m_cpp_extension = value;
    break;

  case ConfigItem::debug:
    m_debug = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::debug_dir:
    m_debug_dir = value;
    break;

  case ConfigItem::debug_level:
    m_debug_level = util::value_or_throw<core::Error>(
      util::parse_unsigned(value, std::nullopt, std::nullopt, "debug level"));
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
    m_extra_files_to_hash = value;
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
    m_ignore_headers_in_manifest = value;
    break;

  case ConfigItem::ignore_options:
    m_ignore_options = value;
    break;

  case ConfigItem::inode_cache:
    m_inode_cache = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::keep_comments_cpp:
    m_keep_comments_cpp = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::log_file:
    m_log_file = value;
    break;

  case ConfigItem::max_files:
    m_max_files = util::value_or_throw<core::Error>(
      util::parse_unsigned(value, std::nullopt, std::nullopt, "max_files"));
    break;

  case ConfigItem::max_size: {
    const auto [size, prefix_type] =
      util::value_or_throw<core::Error>(util::parse_size(value));
    m_max_size = size;
    m_size_prefix_type = prefix_type;
    break;
  }

  case ConfigItem::msvc_dep_prefix:
    m_msvc_dep_prefix = value;
    break;

  case ConfigItem::namespace_:
    m_namespace = value;
    break;

  case ConfigItem::path:
    m_path = value;
    break;

  case ConfigItem::pch_external_checksum:
    m_pch_external_checksum = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::prefix_command:
    m_prefix_command = value;
    break;

  case ConfigItem::prefix_command_cpp:
    m_prefix_command_cpp = value;
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

  case ConfigItem::remote_only:
    m_remote_only = parse_bool(value, env_var_key, negate);
    break;

  case ConfigItem::remote_storage:
    m_remote_storage = value;
    break;

  case ConfigItem::reshare:
    m_reshare = parse_bool(value, env_var_key, negate);
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

  case ConfigItem::stats_log:
    m_stats_log = value;
    break;

  case ConfigItem::temporary_dir:
    m_temporary_dir = value;
    m_temporary_dir_configured_explicitly = true;
    break;

  case ConfigItem::umask:
    if (!value.empty()) {
      m_umask = util::value_or_throw<core::Error>(util::parse_umask(value));
    }
    break;
  }

  const std::string canonical_key = it->second.alias ? *it->second.alias : key;
  const auto& [element, inserted] = m_origins.emplace(canonical_key, origin);
  if (!inserted) {
    element->second = origin;
  }
}

void
Config::check_key_tables_consistency()
{
  for (const auto& [key, value] : k_env_variable_table) {
    if (k_config_key_table.find(value) == k_config_key_table.end()) {
      throw core::Error(
        FMT("env var {} mapped to {} which is missing from k_config_key_table",
            key,
            value));
    }
  }
}

std::string
Config::default_temporary_dir() const
{
  static const std::string run_user_tmp_dir = [] {
#ifndef _WIN32
    const char* const xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime_dir && DirEntry(xdg_runtime_dir).is_directory()) {
      auto dir = FMT("{}/ccache-tmp", xdg_runtime_dir);
      if (fs::create_directories(dir) && access(dir.c_str(), W_OK) == 0) {
        return dir;
      }
    }
#endif
    return std::string();
  }();
  return !run_user_tmp_dir.empty() ? run_user_tmp_dir : m_cache_dir + "/tmp";
}
