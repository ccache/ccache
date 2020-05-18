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

#include "Args.hpp"
#include "ArgsInfo.hpp"
#include "Config.hpp"
#include "File.hpp"
#include "MiniTrace.hpp"
#include "NonCopyable.hpp"
#include "ccache.hpp"
#include "hash.hpp"

#include "third_party/nonstd/optional.hpp"
#include "third_party/nonstd/string_view.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class SignalHandler;

class Context : NonCopyable
{
public:
  Context();
  ~Context();

  ArgsInfo args_info;
  Config config;

  // Current working directory as returned by getcwd(3).
  std::string actual_cwd;

  // Current working directory according to $PWD (falling back to getcwd(3)).
  std::string apparent_cwd;

  // The original argument list.
  Args orig_args;

  // Name (represented as a struct digest) of the file containing the cached
  // result.
  const struct digest& result_name() const;

  // Full path to the file containing the result
  // (cachedir/a/b/cdef[...]-size.result).
  const std::string& result_path() const;

  // Full path to the file containing the manifest
  // (cachedir/a/b/cdef[...]-size.manifest).
  const std::string& manifest_path() const;

  // Time of compilation. Used to see if include files have changed after
  // compilation.
  time_t time_of_compilation = 0;

  // Files included by the preprocessor and their hashes.
  // Key: file path. Value: struct digest.
  std::unordered_map<std::string, digest> included_files;

  // Uses absolute path for some include files.
  bool has_absolute_include_headers = false;

  // The name of the temporary preprocessed file.
  std::string i_tmpfile;

  // The name of the cpp stderr file.
  std::string cpp_stderr;

  // Name (represented as a struct digest) of the file containing the manifest
  // for the cached result.
  const struct digest& manifest_name() const;

  // The stats file to use for the manifest.
  const std::string& manifest_stats_file() const;

  // Compiler guessing is currently only based on the compiler name, so nothing
  // should hard-depend on it if possible.
  GuessedCompiler guessed_compiler = GuessedCompiler::unknown;

  // The .gch/.pch/.pth file used for compilation.
  std::string included_pch_file;

  // Headers (or directories with headers) to ignore in manifest mode.
  std::vector<std::string> ignore_header_paths;

  // Full path to the statistics file in the subdirectory where the cached
  // result belongs (<cache_dir>/<x>/stats).
  const std::string& stats_file() const;

  // Statistics which get written into the `stats_file` upon exit.
  Counters counter_updates;

  // PID of currently executing compiler that we have started, if any. 0 means
  // no ongoing compilation.
  pid_t compiler_pid = 0;

  // Files used by the hash debugging functionality.
  std::vector<File> hash_debug_files;

#ifdef MTR_ENABLED
  // Internal tracing.
  std::unique_ptr<MiniTrace> mini_trace;
#endif

  void set_manifest_name(const struct digest& name);
  void set_result_name(const struct digest& name);

  // Register a temporary file to remove at program exit.
  void register_pending_tmp_file(const std::string& path);

private:
  nonstd::optional<struct digest> m_manifest_name;
  std::string m_manifest_path;
  std::string m_manifest_stats_file;

  nonstd::optional<struct digest> m_result_name;
  std::string m_result_path;
  mutable std::string m_result_stats_file;

  // [Start of variables touched by the signal handler]

  // Temporary files to remove at program exit.
  std::vector<std::string> m_pending_tmp_files;

  // [End of variables touched by the signal handler]

  friend SignalHandler;
  void unlink_pending_tmp_files();
  void unlink_pending_tmp_files_signal_safe(); // called from signal handler

  void set_path_and_stats_file(const struct digest& name,
                               nonstd::string_view suffix,
                               std::string& path_var,
                               std::string& stats_file_var);
};

inline const struct digest&
Context::manifest_name() const
{
  return *m_manifest_name;
}

inline const std::string&
Context::manifest_path() const
{
  assert(m_manifest_name); // set_manifest_name must have been called
  return m_manifest_path;
}

inline const std::string&
Context::manifest_stats_file() const
{
  assert(m_manifest_name); // set_manifest_name must have been called
  return m_manifest_stats_file;
}

inline const struct digest&
Context::result_name() const
{
  return *m_result_name;
}

inline const std::string&
Context::result_path() const
{
  assert(m_result_name); // set_result_name must have been called
  return m_result_path;
}
