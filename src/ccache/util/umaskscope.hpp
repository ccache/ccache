// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
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

#include <sys/types.h>

#include <optional>

namespace util {

// This class sets a new (process-global) umask and restores the previous umask
// when destructed.
class UmaskScope
{
public:
  UmaskScope(std::optional<mode_t> new_umask);
  ~UmaskScope();

  void release();

private:
  std::optional<mode_t> m_saved_umask = std::nullopt;
};

inline UmaskScope::~UmaskScope()
{
  release();
}

} // namespace util
