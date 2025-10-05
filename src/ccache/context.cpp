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

#include "context.hpp"

#include <ccache/hashutil.hpp>
#include <ccache/signalhandler.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/process.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/time.hpp>
#include <ccache/util/wincompat.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>
#include <string>
#include <vector>

namespace fs = util::filesystem;

Context::Context()
  : actual_cwd(fs::current_path().value_or("")),
    apparent_cwd(util::apparent_cwd(actual_cwd)),
    storage(config),
#ifdef INODE_CACHE_SUPPORTED
    inode_cache(config),
#endif
    time_of_invocation(util::now())
{
}

void
Context::initialize(util::Args&& compiler_and_args,
                    const std::vector<std::string>& cmdline_config_settings)
{
  orig_args = std::move(compiler_and_args);
  config.read(cmdline_config_settings);
  util::logging::init(config.debug(), config.log_file());
  ignore_header_paths =
    util::split_path_list(config.ignore_headers_in_manifest());
  set_ignore_options(util::split_into_strings(config.ignore_options(), " "));

  // Set default umask for all files created by ccache from now on (if
  // configured to). This is intentionally done after calling Logging::init so
  // that the log file won't be affected by the umask but before creating the
  // initial configuration file. The intention is that all files and directories
  // in the cache directory should be affected by the configured umask and that
  // no other files and directories should.
  auto mask = config.umask();
  if (mask) {
    original_umask = util::set_umask(*mask);
  }
}

Context::~Context()
{
  unlink_pending_tmp_files();
}

void
Context::register_pending_tmp_file(const fs::path& path)
{
  SignalHandlerBlocker signal_handler_blocker;

  m_pending_tmp_files.push_back(path);
}

void
Context::unlink_pending_tmp_files_signal_safe()
{
  for (auto it = m_pending_tmp_files.rbegin(); it != m_pending_tmp_files.rend();
       ++it) {
    // Don't call util::remove or fs::remove since they are not signal safe.
    unlink(util::pstr(*it).c_str());
  }
  // Don't clear m_pending_tmp_files since this method must be signal safe.
}

void
Context::unlink_pending_tmp_files()
{
  SignalHandlerBlocker signal_handler_blocker;

  for (auto it = m_pending_tmp_files.rbegin(); it != m_pending_tmp_files.rend();
       ++it) {
    std::ignore = util::remove(*it, util::LogFailure::no);
  }
  m_pending_tmp_files.clear();
}

void
Context::set_ignore_options(const std::vector<std::string>& options)
{
  for (const std::string& option : options) {
    size_t n_wildcards = std::count(option.cbegin(), option.cend(), '*');
    if (n_wildcards == 0 || (n_wildcards == 1 && option.back() == '*')) {
      m_ignore_options.push_back(option);
    } else {
      LOG("Skipping malformed ignore_options item: {}", option);
      continue;
    }
  }
}
