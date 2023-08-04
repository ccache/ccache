// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
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
#include "fmtmacros.hpp"

#include <core/exceptions.hpp>
#include <util/TemporaryFile.hpp>
#include <util/TimePoint.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/wincompat.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace fs = util::filesystem;

MiniTrace::MiniTrace(const ArgsInfo& args_info)
  : m_args_info(args_info),
    m_trace_id(reinterpret_cast<void*>(getpid()))
{
  auto tmp_dir = fs::temp_directory_path();
  if (!tmp_dir) {
    tmp_dir = "/tmp";
  }
  auto tmp_file = util::value_or_throw<core::Fatal>(
    util::TemporaryFile::create(*tmp_dir / "ccache-trace"));
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
    util::copy_file(m_tmp_trace_file, m_args_info.output_obj + ".ccache-trace");
  }
  util::remove(m_tmp_trace_file);
}
