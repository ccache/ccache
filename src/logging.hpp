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

#include "third_party/fmt/printf.h"

#include <string>

class Config;

void init_log(const Config& config);
// void cc_log(const char* format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_log_old(const char* format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_bulklog(const char* format, ...) ATTR_FORMAT(printf, 1, 2);
void cc_log_argv(const char* prefix, char** argv);
void cc_dump_debug_log_buffer(const char* path);

template<typename S>
using fmt_char_t = typename fmt::internal::char_t_impl<S>::type;

template<typename S,
         typename... Args,
         typename Char = typename std::
           enable_if<fmt::internal::is_string<S>::value, fmt_char_t<S>>::type>
inline void
cc_log(const S& format, const Args&... args)
{
  using context = fmt::basic_printf_context_t<Char>;
  std::string s = fmt::vsprintf(fmt::to_string_view(format),
                                {fmt::make_format_args<context>(args...)});
  cc_log_old("%s", s.c_str());
}

template<typename S, typename... Args, typename Char = fmt_char_t<S>>
inline void
cc_fmt(const S& format_str, Args&&... args)
{
  std::string s = fmt::internal::vformat(
    fmt::to_string_view(format_str),
    {fmt::internal::make_args_checked<Args...>(format_str, args...)});
  cc_log_old("%s", s.c_str());
}
