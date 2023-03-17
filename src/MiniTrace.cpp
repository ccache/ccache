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

#include "MiniTrace.hpp"

#include "ArgsInfo.hpp"
#include "TemporaryFile.hpp"
#include "Util.hpp"
#include "fmtmacros.hpp"

#include <core/wincompat.hpp>
#include <util/TimePoint.hpp>

#include <limits.h> // NOLINT: PATH_MAX is defined in limits.h

#if __has_include(<unistd.h>)
#  include <unistd.h>
#endif

namespace {

std::string
get_system_tmp_dir()
{
#ifndef _WIN32
  const char* tmpdir = getenv("TMPDIR");
  if (tmpdir) {
    return tmpdir;
  }
#else
  static char dirbuf[PATH_MAX];
  DWORD retval = GetTempPath(PATH_MAX, dirbuf);
  if (retval > 0 && retval < PATH_MAX) {
    return dirbuf;
  }
#endif
  return "/tmp";
}

} // namespace

MiniTrace::MiniTrace(const ArgsInfo& args_info)
  : m_args_info(args_info),
    m_trace_id(reinterpret_cast<void*>(getpid()))
{
  TemporaryFile tmp_file(get_system_tmp_dir() + "/ccache-trace");
  m_tmp_trace_file = tmp_file.path;

  mtr_init(m_tmp_trace_file.c_str());
  auto now = util::TimePoint::now();
  m_start_time = FMT("{}.{:06}", now.sec(), now.nsec_decimal_part() / 1000);
  MTR_INSTANT_C("", "", "time", m_start_time.c_str());
  MTR_META_PROCESS_NAME("ccache");
  MTR_START("program", "ccache", m_trace_id);
}

MiniTrace::~MiniTrace()
{
  MTR_FINISH("program", "ccache", m_trace_id);
  mtr_flush();
  mtr_shutdown();

  if (!m_args_info.output_obj.empty()) {
    Util::copy_file(m_tmp_trace_file, m_args_info.output_obj + ".ccache-trace");
  }
  Util::unlink_tmp(m_tmp_trace_file);
}
