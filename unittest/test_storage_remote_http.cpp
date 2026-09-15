// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include <ccache/storage/remote/httpstorage.hpp>

#include <cxxurl/url.hpp>
#include <doctest/doctest.h>

TEST_SUITE_BEGIN("storage::remote::HttpStorage");

TEST_CASE("create HTTPS HTTP storage backend")
{
  storage::remote::HttpStorage storage;
  CHECK_NOTHROW(storage.create_backend(
    Url("https://cache.example.invalid/cache"), {}, {{}}));
}

TEST_SUITE_END();
