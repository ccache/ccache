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

#include "stats.hpp"

#include <stdexcept>

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
  using ErrorBase::ErrorBase;
};

// Throw a FatalError to make ccache print the error message to stderr and exit
// with a non-zero exit code.
class FatalError : public ErrorBase
{
  using ErrorBase::ErrorBase;
};

// Throw a Failure to make ccache fall back to running the real compiler. Also
// updates statistics counter `stat` if it's not STATS_NONE.
class Failure : public std::exception
{
public:
  Failure(enum stats stat = STATS_NONE);

  enum stats stat() const;

private:
  enum stats m_stat;
};

inline Failure::Failure(enum stats stat) : m_stat(stat)
{
}

inline enum stats
Failure::stat() const
{
  return m_stat;
}
