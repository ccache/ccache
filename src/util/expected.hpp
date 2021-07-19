// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include <utility>

namespace util {

// --- Interface ---

// Return value of `value` (where `T` typically is `nonstd::expected`) or throw
// an exception of type `E` with a `T::error_type` as the argument.
template<typename E, typename T>
typename T::value_type value_or_throw(const T& value);
template<typename E, typename T>
typename T::value_type value_or_throw(T&& value);

// --- Inline implementations ---

template<typename E, typename T>
inline typename T::value_type
value_or_throw(const T& value)
{
  if (value) {
    return *value;
  } else {
    throw E(value.error());
  }
}

template<typename E, typename T>
inline typename T::value_type
value_or_throw(T&& value)
{
  if (value) {
    return std::move(*value);
  } else {
    throw E(value.error());
  }
}

} // namespace util
