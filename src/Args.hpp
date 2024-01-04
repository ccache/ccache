// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

#include <util/NonCopyable.hpp>

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Args
{
public:
  enum class AtFileFormat {
    gcc,  // '\'' and '"' quote, '\\' escapes any character
    msvc, // '"' quotes, '\\' escapes only '"' and '\\'
  };

  Args() = default;
  Args(const Args& other) = default;
  Args(Args&& other) noexcept;

  static Args from_argv(int argc, const char* const* argv);
  static Args from_string(std::string_view command);

  static std::optional<Args>
  from_atfile(const std::string& filename,
              AtFileFormat format = AtFileFormat::gcc);

  Args& operator=(const Args& other) = default;
  Args& operator=(Args&& other) noexcept;

  bool operator==(const Args& other) const;
  bool operator!=(const Args& other) const;

  bool empty() const;
  size_t size() const;
  const std::string& operator[](size_t i) const;
  std::string& operator[](size_t i);

  // Return the argument list as a vector of raw string pointers. Callers can
  // use `const_cast<char* const*>(args.to_argv().data())` to get an array
  // suitable to pass to e.g. execv(2).
  std::vector<const char*> to_argv() const;

  // Return a space-delimited argument list in string form. No quoting of spaces
  // in arguments is performed.
  std::string to_string() const;

  // Remove last argument equal to `arg`, if any.
  void erase_last(std::string_view arg);

  // Remove all arguments with prefix `prefix`.
  void erase_with_prefix(std::string_view prefix);

  // Insert arguments in `args` at position `index`.
  void insert(size_t index, const Args& args);

  // Remove the last `count` arguments.
  void pop_back(size_t count = 1);

  // Remove the first `count` arguments.
  void pop_front(size_t count = 1);

  // Add `arg` to the end.
  void push_back(const std::string& arg);

  // Add `args` to the end.
  void push_back(const Args& args);

  // Add `arg` to the front.
  void push_front(const std::string& arg);

  // Replace the argument at `index` with all arguments in `args`.
  void replace(size_t index, const Args& args);

private:
  std::deque<std::string> m_args;
};

inline bool
Args::operator==(const Args& other) const
{
  return m_args == other.m_args;
}

inline bool
Args::operator!=(const Args& other) const
{
  return m_args != other.m_args;
}

inline bool
Args::empty() const
{
  return m_args.empty();
}

inline size_t
Args::size() const
{
  return m_args.size();
}

inline const std::string&
Args::operator[](size_t i) const
{
  return m_args[i];
}

inline std::string&
Args::operator[](size_t i)
{
  return m_args[i];
}
