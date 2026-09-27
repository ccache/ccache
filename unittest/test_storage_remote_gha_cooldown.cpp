// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include "testutil.hpp"

#include <ccache/storage/remote/ghacooldown.hpp>

#include <doctest/doctest.h>

using namespace std::chrono_literals;

TEST_SUITE_BEGIN("storage::remote::GhaWriteCooldown");

TEST_CASE("persist GHA write cooldown")
{
  TestUtil::TestContext test_context;
  storage::remote::detail::GhaWriteCooldown first("gha-cooldown", "test");
  CHECK_FALSE(first.is_active());
  first.set(60s);

  storage::remote::detail::GhaWriteCooldown second("gha-cooldown", "test");
  CHECK(second.is_active());
}

TEST_CASE("parse GHA retry interval")
{
  CHECK(storage::remote::detail::gha_write_cooldown_duration("120") == 120s);
  CHECK(storage::remote::detail::gha_write_cooldown_duration("0") == 60s);
  CHECK(storage::remote::detail::gha_write_cooldown_duration("invalid") == 60s);
}

TEST_SUITE_END();
