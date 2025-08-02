// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/format.hpp>

#include <tl/expected.hpp>

#include <string_view>
#include <utility>

namespace util {

// --- Interface ---

// Return value of `value` (where `T` typically is `tl::expected`) or throw
// an exception of type `E` with a `T::error_type` as the argument.
template<typename E, typename T>
typename T::value_type value_or_throw(const T& value);
template<typename E, typename T>
typename T::value_type value_or_throw(T&& value);

// Like above for with `prefix` added to the error message.
template<typename E, typename T>
typename T::value_type value_or_throw(const T& value, std::string_view prefix);
template<typename E, typename T>
typename T::value_type value_or_throw(T&& value, std::string_view prefix);

// Throw an exception of type `E` with a `T::error_type` as the argument if
// `value` is false.
template<typename E, typename T> void throw_on_error(const T& value);

// Like above for with `prefix` added to the error message.
template<typename E, typename T>
void throw_on_error(const T& value, std::string_view prefix);

#define TRY(x_)                                                                \
  do {                                                                         \
    const auto result_ = x_;                                                   \
    if (!result_) {                                                            \
      return tl::unexpected(result_.error());                                  \
    }                                                                          \
  } while (false)

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

template<typename E, typename T>
inline typename T::value_type
value_or_throw(const T& value, std::string_view prefix)
{
  if (value) {
    return *value;
  } else {
    throw E(FMT("{}{}", prefix, value.error()));
  }
}

template<typename E, typename T>
inline typename T::value_type
value_or_throw(T&& value, std::string_view prefix)
{
  if (value) {
    return std::move(*value);
  } else {
    throw E(FMT("{}{}", prefix, value.error()));
  }
}

template<typename E, typename T>
inline void
throw_on_error(const T& value)
{
  if (!value) {
    throw E(value.error());
  }
}

// Like above for with `prefix` added to the error message.
template<typename E, typename T>
inline void
throw_on_error(const T& value, std::string_view prefix)
{
  if (!value) {
    throw E(FMT("{}{}", prefix, value.error()));
  }
}

} // namespace util
