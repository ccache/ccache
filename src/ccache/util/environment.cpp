// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#include "environment.hpp"

#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/wincompat.hpp>

namespace fs = util::filesystem;

namespace util {

tl::expected<std::string, std::string>
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
        return tl::unexpected(
          FMT("syntax error: missing '}}' after \"{}\"", left));
      }
      if (right == left) {
        // Special case: don't consider a single $ the left of a variable.
        result += '$';
        --right;
      } else {
        std::string name(left, right - left);
        const char* value = getenv(name.c_str());
        if (!value) {
          return tl::unexpected(
            FMT("environment variable \"{}\" not set", name));
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

#ifdef _WIN32

static std::optional<std::wstring>
wide_getenv(const char* name)
{
  std::vector<wchar_t> wname(strlen(name) + 1);
  size_t n = mbstowcs(wname.data(), name, wname.size());
  if (n == static_cast<size_t>(-1)) {
    return std::nullopt;
  }

  std::vector<wchar_t> value(1024);
  auto len = GetEnvironmentVariableW(wname.data(), value.data(), value.size());
  if (len == 0) {
    // Variable not set.
    return std::nullopt;
  }
  if (len >= value.size()) {
    // len is the number of needed characters including the terminating null.
    value.resize(len);
    len = GetEnvironmentVariableW(wname.data(), value.data(), value.size());
  }
  // len is the number of characters excluding the terminating null
  return std::wstring(value.data(), len);
}

std::optional<fs::path>
getenv_path(const char* name)
{
  auto value = wide_getenv(name);
  return value ? std::optional(fs::path(*value)) : std::nullopt;
}

std::vector<fs::path>
getenv_path_list(const char* name)
{
  auto value = wide_getenv(name);
  if (!value) {
    return {};
  }

  std::vector<fs::path> result;
  std::wstring_view view(*value);
  size_t left = 0;
  while (left < view.size()) {
    size_t right = view.find(';', left);
    if (right == std::wstring_view::npos) {
      right = view.length();
    }
    std::wstring_view path = view.substr(left, right - left);
    if (!path.empty()) {
      result.push_back(fs::path(path));
    }
    if (right == std::wstring_view::npos) {
      break;
    }
    left = right + 1;
  }
  return result;
}

#else // _WIN32

std::optional<fs::path>
getenv_path(const char* name)
{
  const char* value = getenv(name);
  return value ? std::optional(value) : std::nullopt;
}

std::vector<fs::path>
getenv_path_list(const char* name)
{
  const char* value = getenv(name);
  if (!value) {
    return {};
  }

  auto strings = split_into_views(value, ":");
  std::vector<fs::path> paths;
  std::copy(strings.cbegin(), strings.cend(), std::back_inserter(paths));
  return paths;
}

#endif //_WIN32

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
