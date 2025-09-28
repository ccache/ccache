// Copyright (C) 2019-2024 Joel Rosdahl and other contributors
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

#include "progressbar.hpp"

#include <ccache/util/assertions.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/wincompat.hpp>

#ifdef _WIN32
#else
#  include <sys/ioctl.h>
#endif

#ifdef __sun
#  include <termios.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <algorithm>

namespace {

const size_t k_max_width = 120;

size_t
get_terminal_width()
{
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO info;
  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
  return info.srWindow.Right - info.srWindow.Left;
#else
  struct winsize winsize;
  ioctl(0, TIOCGWINSZ, &winsize);
  return winsize.ws_col;
#endif
}

} // namespace

ProgressBar::ProgressBar(const std::string& header)
  : m_header(header),
    m_width(std::min(k_max_width, get_terminal_width())),
    m_stdout_is_a_terminal(isatty(STDOUT_FILENO))
{
  update(0.0);
}

void
ProgressBar::update(double value)
{
  if (!m_stdout_is_a_terminal) {
    return;
  }

  DEBUG_ASSERT(value >= 0.0);
  DEBUG_ASSERT(value <= 1.0);

  int16_t new_value = static_cast<int16_t>(1000 * value);
  if (new_value == m_current_value) {
    return;
  }
  m_current_value = new_value;

  double new_value_percent = new_value / 10.0;

  size_t first_part_width = m_header.size() + 10;
  if (first_part_width + 10 > m_width) {
    // The progress bar would be less than 10 characters, so just print the
    // percentage.
    PRINT(stdout, "\r{} {:5.1f}%", m_header, new_value_percent);
  } else {
    size_t total_bar_width = m_width - first_part_width;
    size_t filled_bar_width = static_cast<size_t>(
      (new_value_percent / 100) * static_cast<double>(total_bar_width));
    size_t unfilled_bar_width = total_bar_width - filled_bar_width;
    PRINT(stdout,
          "\r{} {:5.1f}% [{:=<{}}{: <{}}]",
          m_header,
          new_value / 10.0,
          "",
          filled_bar_width,
          "",
          unfilled_bar_width);
  }

  std::ignore = fflush(stdout);
}
