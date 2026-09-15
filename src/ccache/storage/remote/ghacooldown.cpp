// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include "ghacooldown.hpp"

#include <ccache/core/atomicfile.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/time.hpp>
#include <ccache/util/xxh3_64.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>

namespace fs = util::filesystem;

namespace storage::remote::detail {

namespace {

constexpr auto k_default_cooldown = std::chrono::seconds(60);

std::filesystem::path
cooldown_path(const std::filesystem::path& cache_dir,
              const std::string_view identity)
{
  util::XXH3_64 hash;
  hash.update(identity.data(), identity.size());
  return cache_dir / "remote-storage"
         / FMT("gha-write-cooldown-{:016x}", hash.digest());
}

std::optional<int64_t>
read_expiration(const std::filesystem::path& path)
{
  const auto content = util::read_file<std::string>(path);
  if (!content) {
    return std::nullopt;
  }

  int64_t expiration = 0;
  const auto [end, error] = std::from_chars(
    content->data(), content->data() + content->size(), expiration);
  return error == std::errc() && end == content->data() + content->size()
           ? std::optional<int64_t>(expiration)
           : std::nullopt;
}

} // namespace

GhaWriteCooldown::GhaWriteCooldown(const std::filesystem::path& cache_dir,
                                   const std::string_view identity)
  : m_path(cooldown_path(cache_dir, identity))
{
}

bool
GhaWriteCooldown::is_active()
{
  const auto expiration = read_expiration(m_path);
  if (!expiration) {
    return false;
  }
  if (*expiration > util::sec(util::now())) {
    return true;
  }
  std::ignore = util::remove(m_path, util::LogFailure::no);
  return false;
}

void
GhaWriteCooldown::set(const std::chrono::seconds duration)
{
  if (const auto result = fs::create_directories(m_path.parent_path()); !result) {
    LOG("CCACHE_NG-WARN-GHA-0020: unable to persist GHA write cooldown");
    return;
  }

  try {
    core::AtomicFile file(m_path, core::AtomicFile::Mode::text);
    file.write(FMT("{}", util::sec(util::now() + duration)));
    file.commit();
  } catch (const core::Error&) {
    LOG("CCACHE_NG-WARN-GHA-0020: unable to persist GHA write cooldown");
  }
}

std::chrono::seconds
gha_write_cooldown_duration(const std::string_view retry_after)
{
  int64_t seconds = 0;
  const auto [end, error] = std::from_chars(retry_after.data(),
                                            retry_after.data() + retry_after.size(),
                                            seconds);
  if (error != std::errc() || end != retry_after.data() + retry_after.size()
      || seconds <= 0) {
    return k_default_cooldown;
  }
  return std::chrono::seconds(seconds);
}

} // namespace storage::remote::detail
