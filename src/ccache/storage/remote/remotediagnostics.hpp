// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#pragma once

#include <ccache/util/logging.hpp>

#include <string_view>

namespace storage::remote::detail {

inline void
log_diagnostic(const std::string_view code, const std::string_view message)
{
  LOG("{}: {}", code, message);
}

} // namespace storage::remote::detail
