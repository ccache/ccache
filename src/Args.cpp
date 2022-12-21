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

#include "Args.hpp"

#include "Util.hpp"

#include <Logging.hpp>
#include <core/exceptions.hpp>
#include <util/file.hpp>
#include <util/string.hpp>

Args::Args(Args&& other) noexcept : m_args(std::move(other.m_args))
{
}

Args
Args::from_argv(int argc, const char* const* argv)
{
  Args args;
  args.m_args.assign(argv, argv + argc);
  return args;
}

Args
Args::from_string(std::string_view command)
{
  Args args;
  for (const std::string& word : Util::split_into_strings(command, " \t\r\n")) {
    args.push_back(word);
  }
  return args;
}

std::optional<Args>
Args::from_atfile(const std::string& filename, AtFileFormat format)
{
  const auto argtext = util::read_file<std::string>(filename);
  if (!argtext) {
    LOG("Failed to read atfile {}: {}", filename, argtext.error());
    return std::nullopt;
  }

  Args args;
  auto pos = argtext->c_str();
  std::string argbuf;
  argbuf.resize(argtext->length() + 1);
  auto argpos = argbuf.begin();

  // Used to track quoting state; if \0 we are not inside quotes. Otherwise
  // stores the quoting character that started it for matching the end quote.
  char quoting = '\0';

  while (true) {
    switch (*pos) {
    case '\\':
      pos++;
      switch (format) {
      case AtFileFormat::gcc:
        if (*pos == '\0') {
          continue;
        }
        break;
      case AtFileFormat::msvc:
        if (*pos != '"') {
          pos--;
        }
        break;
      }
      break;

    case '\'':
      if (format == AtFileFormat::msvc) {
        break;
      }
      [[fallthrough]];

    case '"':
      if (quoting != '\0') {
        if (quoting == *pos) {
          quoting = '\0';
          pos++;
          continue;
        } else {
          break;
        }
      } else {
        quoting = *pos;
        pos++;
        continue;
      }

    case '\n':
    case '\r':
    case '\t':
    case ' ':
      if (quoting) {
        break;
      }
      [[fallthrough]];

    case '\0':
      // End of token
      *argpos = '\0';
      if (argbuf[0] != '\0') {
        args.push_back(argbuf.substr(0, argbuf.find('\0')));
      }
      argpos = argbuf.begin();
      if (*pos == '\0') {
        return args;
      } else {
        pos++;
        continue;
      }
    }

    *argpos = *pos;
    pos++;
    argpos++;
  }
}

Args&
Args::operator=(Args&& other) noexcept
{
  if (&other != this) {
    m_args = std::move(other.m_args);
  }
  return *this;
}

std::vector<const char*>
Args::to_argv() const
{
  std::vector<const char*> result;
  result.reserve(m_args.size() + 1);
  for (const auto& arg : m_args) {
    result.push_back(arg.c_str());
  }
  result.push_back(nullptr);
  return result;
}

std::string
Args::to_string() const
{
  std::string result;
  for (const auto& arg : m_args) {
    if (!result.empty()) {
      result += ' ';
    }
    result += arg;
  }
  return result;
}

void
Args::erase_last(std::string_view arg)
{
  const auto it = std::find(m_args.rbegin(), m_args.rend(), arg);
  if (it != m_args.rend()) {
    m_args.erase(std::next(it).base());
  }
}

void
Args::erase_with_prefix(std::string_view prefix)
{
  m_args.erase(std::remove_if(m_args.begin(),
                              m_args.end(),
                              [&prefix](const auto& s) {
                                return util::starts_with(s, prefix);
                              }),
               m_args.end());
}

void
Args::insert(size_t index, const Args& args)
{
  if (args.size() == 0) {
    return;
  }
  m_args.insert(m_args.begin() + index, args.m_args.begin(), args.m_args.end());
}

void
Args::pop_back(size_t count)
{
  m_args.erase(m_args.end() - count, m_args.end());
}

void
Args::pop_front(size_t count)
{
  m_args.erase(m_args.begin(), m_args.begin() + count);
}

void
Args::push_back(const std::string& arg)
{
  m_args.push_back(arg);
}

void
Args::push_back(const Args& args)
{
  m_args.insert(m_args.end(), args.m_args.begin(), args.m_args.end());
}

void
Args::push_front(const std::string& arg)
{
  m_args.push_front(arg);
}

void
Args::replace(size_t index, const Args& args)
{
  if (args.size() == 1) {
    // Trivial case; replace with 1 element.
    m_args[index] = args[0];
  } else {
    m_args.erase(m_args.begin() + index);
    insert(index, args);
  }
}
