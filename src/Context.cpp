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

#include "Context.hpp"

#include "Counters.hpp"
#include "Logging.hpp"
#include "SignalHandler.hpp"
#include "Util.hpp"
#include "hashutil.hpp"
#include "stats.hpp"

#include <algorithm>
#include <string>
#include <vector>

using Logging::log;
using nonstd::string_view;

Context::Context()
  : actual_cwd(Util::get_actual_cwd()),
    apparent_cwd(Util::get_apparent_cwd(actual_cwd))
#ifdef INODE_CACHE_SUPPORTED
    ,
    inode_cache(config)
#endif
{
}

Context::~Context()
{
  stats_flush(*this);
  unlink_pending_tmp_files();

  // Dump log buffer last to not lose any logs.
  if (config.debug()) {
    const std::string path = fmt::format("{}.ccache-log", args_info.output_obj);
    Logging::dump_log(path);
  }
}

void
Context::set_manifest_name(const Digest& name)
{
  m_manifest_name = name;
  set_path_and_stats_file(
    name, ".manifest", m_manifest_path, m_manifest_stats_file);
}

void
Context::set_result_name(const Digest& name)
{
  m_result_name = name;
  set_path_and_stats_file(name, ".result", m_result_path, m_result_stats_file);
}

const std::string&
Context::stats_file() const
{
  if (m_result_stats_file.empty()) {
    // An empty m_result_stats_file means that set_result_name hasn't been
    // called yet, so we just choose one of stats files in the 16
    // subdirectories.
    m_result_stats_file =
      fmt::format("{}/{:x}/stats", config.cache_dir(), getpid() % 16);
  }
  return m_result_stats_file;
}

void
Context::set_path_and_stats_file(const Digest& name,
                                 string_view suffix,
                                 std::string& path_var,
                                 std::string& stats_file_var) const
{
  std::string name_string = name.to_string();
  path_var = Util::get_path_in_cache(
    config.cache_dir(), config.cache_dir_levels(), name_string, suffix);
  stats_file_var =
    fmt::format("{}/{}/stats", config.cache_dir(), name_string[0]);
}

void
Context::register_pending_tmp_file(const std::string& path)
{
  const SignalHandlerBlocker signal_handler_blocker;

  m_pending_tmp_files.push_back(path);
}

void
Context::unlink_pending_tmp_files_signal_safe()
{
  for (const std::string& path : m_pending_tmp_files) {
    // Don't call Util::unlink_tmp since its log calls aren't signal safe.
    unlink(path.c_str());
  }
  // Don't clear m_pending_tmp_files since this method must be signal safe.
}

void
Context::unlink_pending_tmp_files()
{
  const SignalHandlerBlocker signal_handler_blocker;

  for (const std::string& path : m_pending_tmp_files) {
    Util::unlink_tmp(path, Util::UnlinkLog::ignore_failure);
  }
  m_pending_tmp_files.clear();
}

void
Context::set_ignore_options(const std::vector<std::string>& options)
{
  for (const std::string& option : options) {
    const size_t n_wildcards = std::count(option.cbegin(), option.cend(), '*');
    if (n_wildcards == 0 || (n_wildcards == 1 && option.back() == '*')) {
      m_ignore_options.push_back(option);
    } else {
      log("Skipping malformed ignore_options item: {}", option);
      continue;
    }
  }
}
