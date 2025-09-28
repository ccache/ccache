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

#include <ccache/util/macro.hpp>

#include <functional>

#define DEFER(...)                                                             \
  util::Deferrer UNIQUE_VARNAME(_deferrer_)([&] { (void)__VA_ARGS__; })

namespace util {

class Deferrer
{
public:
  Deferrer(std::function<void()> func);
  ~Deferrer();

private:
  std::function<void()> m_func;
};

inline Deferrer::Deferrer(std::function<void()> func)
  : m_func(func)
{
}

inline Deferrer::~Deferrer()
{
  m_func();
}

} // namespace util
