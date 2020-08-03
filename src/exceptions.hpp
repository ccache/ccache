// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "FormatNonstdStringView.hpp"
#include "stats.hpp"

#include "third_party/fmt/core.h"
#include "third_party/nonstd/optional.hpp"

#include <stdexcept>
#include <string>
#include <utility>

// Don't throw or catch ErrorBase directly, use a subclass.
class ErrorBase : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

// Throw an Error to indicate a potentially non-fatal error that may be caught
// and handled by callers. An uncaught Error that reaches the top level will be
// treated similar to FatalError.
class Error : public ErrorBase
{
public:
  // Special case: If given only one string, don't parse it as a format string.
  Error(const std::string& message);

  // `args` are forwarded to `fmt::format`.
  template<typename... T> inline Error(T&&... args);
};

inline Error::Error(const std::string& message) : ErrorBase(message)
{
}

template<typename... T>
inline Error::Error(T&&... args)
  : ErrorBase(fmt::format(std::forward<T>(args)...))
{
}

// Throw a FatalError to make ccache print the error message to stderr and exit
// with a non-zero exit code.
class FatalError : public ErrorBase
{
public:
  // Special case: If given only one string, don't parse it as a format string.
  FatalError(const std::string& message);

  // `args` are forwarded to `fmt::format`.
  template<typename... T> inline FatalError(T&&... args);
};

inline FatalError::FatalError(const std::string& message) : ErrorBase(message)
{
}

template<typename... T>
inline FatalError::FatalError(T&&... args)
  : ErrorBase(fmt::format(std::forward<T>(args)...))
{
}

// Throw a Failure if ccache did not succeed in getting or putting a result in
// the cache. If `exit_code` is set, just exit with that code directly,
// otherwise execute the real compiler and exit with its exit code. Also updates
// statistics counter `stat` if it's not STATS_NONE.
class Failure : public std::exception
{
public:
  Failure(enum stats stat, nonstd::optional<int> exit_code);

  nonstd::optional<int> exit_code() const;
  enum stats stat() const;

private:
  enum stats m_stat;
  nonstd::optional<int> m_exit_code;
};

inline Failure::Failure(enum stats stat, nonstd::optional<int> exit_code)
  : m_stat(stat), m_exit_code(exit_code)
{
}

inline nonstd::optional<int>
Failure::exit_code() const
{
  return m_exit_code;
}

inline enum stats
Failure::stat() const
{
  return m_stat;
}
