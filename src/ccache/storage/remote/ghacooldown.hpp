// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#pragma once

#include <chrono>
#include <filesystem>
#include <string_view>

namespace storage::remote::detail {

class GhaWriteCooldown
{
public:
  GhaWriteCooldown(const std::filesystem::path& cache_dir,
                   std::string_view identity);

  bool is_active();
  void set(std::chrono::seconds duration);

private:
  std::filesystem::path m_path;
};

std::chrono::seconds gha_write_cooldown_duration(std::string_view retry_after);

} // namespace storage::remote::detail
