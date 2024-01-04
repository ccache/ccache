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

#include "Util.hpp"

#include <util/assertions.hpp>
#include <util/fmtmacros.hpp>

namespace util {

void
handle_failed_assertion(const char* file,
                        size_t line,
                        const char* function,
                        const char* condition)
{
  PRINT(stderr,
        "ccache: {}:{}: {}: failed assertion: {}\n",
        Util::base_name(file),
        line,
        function,
        condition);
  abort();
}
} // namespace util
