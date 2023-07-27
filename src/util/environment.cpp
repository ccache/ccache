// Copyright (C) 2023 Joel Rosdahl and other contributors
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

#include "environment.hpp"

#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/wincompat.hpp>

namespace util {

std::string
expand_environment_variables(const std::string& str)
{
  std::string result;
  const char* left = str.c_str();
  const char* right = left;

  while (*right) {
    if (*right == '$') {
      result.append(left, right - left);

      if (*(right + 1) == '$') {
        result += '$';
        right += 2;
        left = right;
        continue;
      }

      left = right + 1;
      bool curly = *left == '{';
      if (curly) {
        ++left;
      }
      right = left;
      while (isalnum(*right) || *right == '_') {
        ++right;
      }
      if (curly && *right != '}') {
        throw core::Error(FMT("syntax error: missing '}}' after \"{}\"", left));
      }
      if (right == left) {
        // Special case: don't consider a single $ the left of a variable.
        result += '$';
        --right;
      } else {
        std::string name(left, right - left);
        const char* value = getenv(name.c_str());
        if (!value) {
          throw core::Error(FMT("environment variable \"{}\" not set", name));
        }
        result += value;
        if (!curly) {
          --right;
        }
        left = right + 1;
      }
    }
    ++right;
  }

  result += left;
  return result;
}

void
setenv(const std::string& name, const std::string& value)
{
#ifdef HAVE_SETENV
  ::setenv(name.c_str(), value.c_str(), true);
#else
  auto string = FMT("{}={}", name, value);
  putenv(strdup(string.c_str())); // Leak to environment.
#endif
}

void
unsetenv(const std::string& name)
{
#ifdef HAVE_UNSETENV
  ::unsetenv(name.c_str());
#elif defined(_WIN32)
  SetEnvironmentVariable(name.c_str(), NULL);
#else
  putenv(strdup(name.c_str())); // Leak to environment.
#endif
}

} // namespace util
