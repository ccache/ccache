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

#include <ccache/argsinfo.hpp>
#include <ccache/config.hpp>
#include <ccache/core/manifest.hpp>
#include <ccache/hash.hpp>
#include <ccache/storage/storage.hpp>
#include <ccache/util/args.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/filestream.hpp>
#include <ccache/util/noncopyable.hpp>
#include <ccache/util/time.hpp>

#ifdef INODE_CACHE_SUPPORTED
#  include <ccache/inodecache.hpp>
#endif

#include <sys/types.h>

#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class SignalHandler;

class Context : util::NonCopyable
{
public:
  Context();
  ~Context();

  // Read configuration, initialize logging, etc. Typically not called from unit
  // tests.
  void initialize(util::Args&& compiler_and_args,
                  const std::vector<std::string>& cmdline_config_settings);

  ArgsInfo args_info;
  Config config;

  // Current working directory as returned by getcwd(3).
  std::filesystem::path actual_cwd;

  // Current working directory according to $PWD (falling back to getcwd(3)).
  std::filesystem::path apparent_cwd;

  // The original argument list.
  util::Args orig_args;

  // Files included by the preprocessor and their hashes.
  std::unordered_map<std::string, Hash::Digest> included_files;

  // Have we tried and failed to get colored diagnostics?
  bool diagnostics_color_failed = false;

  // The name of the temporary preprocessed file.
  std::filesystem::path i_tmpfile;

  // The preprocessor's stderr output.
  util::Bytes cpp_stderr_data;

  // Headers (or directories with headers) to ignore in manifest mode.
  std::vector<std::filesystem::path> ignore_header_paths;

  // Storage (fronting local and remote storage backends).
  storage::Storage storage;

  // Direct mode manifest.
  core::Manifest manifest;

#ifdef INODE_CACHE_SUPPORTED
  // InodeCache that caches source file hashes when enabled.
  mutable InodeCache inode_cache;
#endif

  // Time of ccache invocation.
  util::TimePoint time_of_invocation;

  // PID of currently executing compiler that we have started, if any. 0 means
  // no ongoing compilation.
  pid_t compiler_pid = 0;

  // Files used by the hash debugging functionality.
  std::vector<util::FileStream> hash_debug_files;

  // Options to ignore for the hash.
  const std::vector<std::string>& ignore_options() const;
  void set_ignore_options(const std::vector<std::string>& options);

  // Original umask before applying the `umask`/`CCACHE_UMASK` configuration, or
  // `nullopt` if there is no such configuration.
  std::optional<mode_t> original_umask;

  // Whether we have added "/showIncludes" ourselves since it's missing and
  // depend mode is enabled.
  bool auto_depend_mode = false;

  // Register a temporary file to remove at program exit.
  void register_pending_tmp_file(const std::filesystem::path& path);

private:
  // Options to ignore for the hash.
  std::vector<std::string> m_ignore_options;

  // [Start of variables touched by the signal handler]

  // Temporary files to remove at program exit.
  std::vector<std::filesystem::path> m_pending_tmp_files;

  // [End of variables touched by the signal handler]

  friend SignalHandler;
  void unlink_pending_tmp_files();
  void unlink_pending_tmp_files_signal_safe(); // called from signal handler
};

inline const std::vector<std::string>&
Context::ignore_options() const
{
  return m_ignore_options;
}
