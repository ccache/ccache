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
#include "SignalHandler.hpp"
#include "Util.hpp"
#include "hashutil.hpp"
#include "logging.hpp"
#include "stats.hpp"

using nonstd::string_view;

Context::Context()
  : actual_cwd(Util::get_actual_cwd()),
    apparent_cwd(Util::get_apparent_cwd(actual_cwd))
{
}

Context::~Context()
{
  stats_flush(this);
  unlink_pending_tmp_files();

  // Dump log buffer last to not lose any logs.
  if (config.debug()) {
    std::string path = fmt::format("{}.ccache-log", args_info.output_obj);
    cc_dump_debug_log_buffer(path.c_str());
  }
}

void
Context::set_manifest_name(const struct digest& name)
{
  m_manifest_name = name;
  set_path_and_stats_file(
    name, ".manifest", m_manifest_path, m_manifest_stats_file);
}

void
Context::set_result_name(const struct digest& name)
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
    m_result_stats_file = fmt::format(
      "{}/{:x}/stats", config.cache_dir(), hash_from_int(getpid()) % 16);
  }
  return m_result_stats_file;
}

void
Context::set_path_and_stats_file(const struct digest& name,
                                 string_view suffix,
                                 std::string& path_var,
                                 std::string& stats_file_var)
{
  char name_string[DIGEST_STRING_BUFFER_SIZE];
  digest_as_string(&name, name_string);
  path_var = Util::get_path_in_cache(
    config.cache_dir(), config.cache_dir_levels(), name_string, suffix);
  stats_file_var =
    fmt::format("{}/{}/stats", config.cache_dir(), name_string[0]);
}

void
Context::register_pending_tmp_file(const std::string& path)
{
  SignalHandlerBlocker signal_handler_blocker;

  m_pending_tmp_files.push_back(path);
}

void
Context::unlink_pending_tmp_files_signal_safe()
{
  for (const std::string& path : m_pending_tmp_files) {
    // Don't call Util::unlink_tmp since its cc_log calls aren't signal safe.
    unlink(path.c_str());
  }
  // Don't clear m_pending_tmp_files since this method must be signal safe.
}

void
Context::unlink_pending_tmp_files()
{
  SignalHandlerBlocker signal_handler_blocker;

  for (const std::string& path : m_pending_tmp_files) {
    Util::unlink_tmp(path, Util::UnlinkLog::ignore_failure);
  }
  m_pending_tmp_files.clear();
}
