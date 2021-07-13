// Copyright (C) 2020-2021 Joel Rosdahl and other contributors
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

#ifdef _WIN32

#  include <core/wincompat.hpp>

#  include <string>

struct tm* localtime_r(time_t* _clock, struct tm* _result);

#  ifdef _MSC_VER
int gettimeofday(struct timeval* tp, struct timezone* tzp);
int asprintf(char** strp, const char* fmt, ...);
#  endif

namespace Win32Util {

// Add ".exe" suffix to `program` if it doesn't already end with ".exe", ".bat"
// or ".sh".
std::string add_exe_suffix(const std::string& program);

// Recreate a Windows command line string based on `argv`. If `prefix` is
// non-empty, add it as the first argument. If `escape_backslashes` is true,
// emit an additional backslash for each backslash that is not preceding '"' and
// is not at the end of `argv[i]` either.
std::string argv_to_string(const char* const* argv,
                           const std::string& prefix,
                           bool escape_backslashes = false);

// Return the error message corresponding to `error_code`.
std::string error_message(DWORD error_code);

// Returns the last NTSTATUS code. (These can be more specific than the
// corresponding Win32 error code.)
NTSTATUS get_last_ntstatus();

} // namespace Win32Util

#endif
