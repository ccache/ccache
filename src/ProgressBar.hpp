// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "core/system.hpp"

#include <string>

class ProgressBar
{
public:
  explicit ProgressBar(const std::string& header);

  // Update progress bar.
  //
  // Parameters:
  // - value: A value between 0.0 (nothing completed) to 1.0 (all completed).
  void update(double value);

private:
  const std::string m_header;
  const size_t m_width;
  const bool m_stdout_is_a_terminal;
  int16_t m_current_value = -1; // trunc(1000 * value)
};
