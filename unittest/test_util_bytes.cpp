// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/bytes.hpp>

#include <doctest/doctest.h>
#include <nonstd/span.hpp>

#include <vector>

TEST_SUITE_BEGIN("util::Bytes");

using util::Bytes;

TEST_CASE("Basics")
{
  Bytes bytes1("abc", 3);

  SUBCASE("Default construction")
  {
    Bytes bytes0;

    CHECK(bytes0.data() == nullptr);
    CHECK(bytes0.size() == 0);
    CHECK(bytes0.capacity() == 0);
  }

  SUBCASE("Sized construction")
  {
    Bytes bytes2(42);

    CHECK(bytes2.data() != nullptr);
    CHECK(bytes2.size() == 42);
    CHECK(bytes2.capacity() == 42);
  }

  SUBCASE("Construction from data and size")
  {
    CHECK(bytes1.data() != nullptr);
    REQUIRE(bytes1.size() == 3);
    REQUIRE(bytes1.capacity() == 3);
    CHECK(bytes1[0] == 'a');
    CHECK(bytes1[1] == 'b');
    CHECK(bytes1[2] == 'c');
  }

  SUBCASE("Construction from span")
  {
    std::vector<uint8_t> vector{'a', 'b', 'c'};
    Bytes bytes2(vector);

    CHECK(bytes2.data() != nullptr);
    REQUIRE(bytes2.size() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
  }

  SUBCASE("Copy construction")
  {
    const Bytes bytes2(bytes1);

    CHECK(bytes2.data() != nullptr);
    CHECK(bytes2.data() != bytes1.data());
    REQUIRE(bytes2.size() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
  }

  SUBCASE("Move construction")
  {
    // cppcheck-suppress constVariablePointer; we're intentionally keeping a
    // copy of the pointer
    const uint8_t* bytes1_orig_data = bytes1.data();
    Bytes bytes2(std::move(bytes1));

    CHECK(bytes1.data() == nullptr);
    CHECK(bytes1.size() == 0);

    CHECK(bytes2.data() != nullptr);
    CHECK(bytes2.data() == bytes1_orig_data);
    REQUIRE(bytes2.size() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
  }

  SUBCASE("Construction from initializer list")
  {
    Bytes bytes2{'a', 'b', 'c'};
    CHECK(bytes2 == bytes1);
  }

  SUBCASE("Copy assignment")
  {
    Bytes bytes2;

    bytes2 = bytes1;
    CHECK(bytes2.data() != nullptr);
    CHECK(bytes2.data() != bytes1.data());
    REQUIRE(bytes2.size() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
  }

  SUBCASE("Move assignment")
  {
    // cppcheck-suppress constVariablePointer; we're intentionally keeping a
    // copy of the pointer
    const uint8_t* bytes1_orig_data = bytes1.data();
    Bytes bytes2;
    bytes2 = std::move(bytes1);

    CHECK(bytes1.data() == nullptr);
    CHECK(bytes1.size() == 0);

    CHECK(bytes2.data() == bytes1_orig_data);
    REQUIRE(bytes2.size() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
  }

  SUBCASE("Assignment from initializer list")
  {
    Bytes bytes2;
    bytes2 = {'a', 'b', 'c'};
    CHECK(bytes2 == bytes1);
  }

  SUBCASE("Non-const operator[]")
  {
    bytes1[1] = 'x';
    CHECK(bytes1[1] == 'x'); // cppcheck-suppress knownConditionTrueFalse
  }

  SUBCASE("Comparison")
  {
    CHECK(bytes1 == bytes1);
    CHECK(!(bytes1 != bytes1));

    Bytes bytes2(bytes1);
    CHECK(bytes2 == bytes1);
    CHECK(!(bytes2 != bytes1));

    Bytes bytes3;
    CHECK(bytes3 != bytes1);
    CHECK(!(bytes3 == bytes1));

    Bytes bytes4("xyz", 3);
    CHECK(bytes4 != bytes1);
    CHECK(!(bytes4 == bytes1));
  }
  SUBCASE("Begin")
  {
    const Bytes bytes2(bytes1);

    CHECK(bytes1.begin() == bytes1.data());
    CHECK(bytes2.begin() == bytes2.data());
    CHECK(bytes1.cbegin() == bytes1.data());
  }

  SUBCASE("end")
  {
    const Bytes bytes2(bytes1);

    CHECK(bytes1.end() == bytes1.data() + bytes1.size());
    CHECK(bytes2.end() == bytes2.data() + bytes2.size());
    CHECK(bytes1.cend() == bytes1.data() + bytes1.size());
  }

  SUBCASE("Clear and empty")
  {
    CHECK(bytes1.size() == 3);
    CHECK(bytes1.capacity() == 3);
    CHECK(!bytes1.empty());

    bytes1.clear();

    CHECK(bytes1.size() == 0);
    CHECK(bytes1.capacity() == 3);
    CHECK(bytes1.empty());
  }

  SUBCASE("Reserve and capacity")
  {
    // cppcheck-suppress constVariablePointer; we're intentionally keeping a
    // copy of the pointer
    const uint8_t* bytes1_orig_data = bytes1.data();
    CHECK(bytes1.size() == 3);
    CHECK(bytes1.capacity() == 3);

    bytes1.reserve(2);
    CHECK(bytes1.size() == 3);
    CHECK(bytes1.capacity() == 3);
    CHECK(bytes1.data() == bytes1_orig_data);

    bytes1.reserve(4);
    CHECK(bytes1.size() == 3);
    CHECK(bytes1.capacity() == 4);
    CHECK(bytes1.data() != bytes1_orig_data);
  }

  SUBCASE("Increase size")
  {
    // cppcheck-suppress constVariablePointer; we're intentionally keeping a
    // copy of the pointer
    const uint8_t* bytes1_orig_data = bytes1.data();
    bytes1.resize(4);
    CHECK(bytes1.data() != bytes1_orig_data);
    CHECK(bytes1.size() == 4);
    CHECK(bytes1.capacity() == 4);
    CHECK(bytes1[0] == 'a');
    CHECK(bytes1[1] == 'b');
    CHECK(bytes1[2] == 'c');
  }

  SUBCASE("Decrease size")
  {
    // cppcheck-suppress constVariablePointer; we're intentionally keeping a
    // copy of the pointer
    const uint8_t* bytes1_orig_data = bytes1.data();
    bytes1.resize(2);
    CHECK(bytes1.data() == bytes1_orig_data);
    CHECK(bytes1.size() == 2);
    CHECK(bytes1.capacity() == 3);
    CHECK(bytes1[0] == 'a');
    CHECK(bytes1[1] == 'b');
  }

  SUBCASE("Insert")
  {
    Bytes bytes2;

    bytes2.insert(bytes2.end(), bytes1.begin(), bytes1.end());
    CHECK(bytes2.size() == 3);
    CHECK(bytes2.capacity() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');

    // Insert at end, reallocating.
    bytes2.insert(bytes2.end(), bytes1.begin(), bytes1.begin() + 1);
    CHECK(bytes2.size() == 4);
    CHECK(bytes2.capacity() == 6);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
    CHECK(bytes2[3] == 'a');

    // Insert at end, not reallocating.
    Bytes bytes3("xyz", 3);
    bytes2.insert(bytes2.end(), bytes3.begin(), bytes3.begin() + 1);
    CHECK(bytes2.size() == 5);
    CHECK(bytes2.capacity() == 6);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
    CHECK(bytes2[3] == 'a');
    CHECK(bytes2[4] == 'x');

    // Insert in middle, reallocating.
    bytes2.insert(bytes2.begin() + 2, bytes3.begin(), bytes3.end());
    CHECK(bytes2.size() == 8);
    CHECK(bytes2.capacity() == 12);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'x');
    CHECK(bytes2[3] == 'y');
    CHECK(bytes2[4] == 'z');
    CHECK(bytes2[5] == 'c');
    CHECK(bytes2[6] == 'a');
    CHECK(bytes2[7] == 'x');

    // Insert in middle, not reallocating.
    bytes2.insert(bytes2.begin() + 1, bytes3.begin(), bytes3.begin() + 2);
    CHECK(bytes2.size() == 10);
    CHECK(bytes2.capacity() == 12);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'x');
    CHECK(bytes2[2] == 'y');
    CHECK(bytes2[3] == 'b');
    CHECK(bytes2[4] == 'x');
    CHECK(bytes2[5] == 'y');
    CHECK(bytes2[6] == 'z');
    CHECK(bytes2[7] == 'c');
    CHECK(bytes2[8] == 'a');
    CHECK(bytes2[9] == 'x');

    // Insert at beginning, reallocating.
    bytes2.insert(bytes2.begin(), bytes3.begin(), bytes3.end());
    CHECK(bytes2.size() == 13);
    CHECK(bytes2.capacity() == 24);
    CHECK(bytes2[0] == 'x');
    CHECK(bytes2[1] == 'y');
    CHECK(bytes2[2] == 'z');
    CHECK(bytes2[3] == 'a');
    CHECK(bytes2[4] == 'x');
    CHECK(bytes2[5] == 'y');
    CHECK(bytes2[6] == 'b');
    CHECK(bytes2[7] == 'x');
    CHECK(bytes2[8] == 'y');
    CHECK(bytes2[9] == 'z');
    CHECK(bytes2[10] == 'c');
    CHECK(bytes2[11] == 'a');
    CHECK(bytes2[12] == 'x');

    // Insert at beginning, not reallocating.
    bytes2.insert(bytes2.begin(), bytes3.begin() + 2, bytes3.begin() + 3);
    CHECK(bytes2.size() == 14);
    CHECK(bytes2.capacity() == 24);
    CHECK(bytes2[0] == 'z');
    CHECK(bytes2[1] == 'x');
    CHECK(bytes2[2] == 'y');
    CHECK(bytes2[3] == 'z');
    CHECK(bytes2[4] == 'a');
    CHECK(bytes2[5] == 'x');
    CHECK(bytes2[6] == 'y');
    CHECK(bytes2[7] == 'b');
    CHECK(bytes2[8] == 'x');
    CHECK(bytes2[9] == 'y');
    CHECK(bytes2[10] == 'z');
    CHECK(bytes2[11] == 'c');
    CHECK(bytes2[12] == 'a');
    CHECK(bytes2[13] == 'x');
  }

  SUBCASE("Insert util::Bytes data and size")
  {
    Bytes bytes2;

    bytes2.insert(bytes2.end(), bytes1.data(), bytes1.size());
    CHECK(bytes2.size() == 3);
    CHECK(bytes2.capacity() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
  }

  SUBCASE("Insert const char* first and last")
  {
    Bytes bytes2;
    std::string data("abc");

    bytes2.insert(bytes2.end(), data.data(), data.data() + data.size());
    CHECK(bytes2.size() == 3);
    CHECK(bytes2.capacity() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
  }

  SUBCASE("Insert const char* data and size")
  {
    Bytes bytes2;
    std::string data("abc");

    bytes2.insert(bytes2.end(), data.data(), data.size());
    CHECK(bytes2.size() == 3);
    CHECK(bytes2.capacity() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
  }

  SUBCASE("Insert span")
  {
    Bytes bytes2;
    nonstd::span<const uint8_t> span(bytes1.begin(), bytes1.end());

    bytes2.insert(bytes2.end(), span);
    CHECK(bytes2.size() == 3);
    CHECK(bytes2.capacity() == 3);
    CHECK(bytes2[0] == 'a');
    CHECK(bytes2[1] == 'b');
    CHECK(bytes2[2] == 'c');
  }
}

TEST_CASE("Conversion to span")
{
  Bytes bytes;
  bytes.resize(42);

  SUBCASE("Const span")
  {
    nonstd::span<const uint8_t> span(bytes);
    CHECK(span.data() == bytes.data());
    CHECK(span.size() == bytes.size());
  }

  SUBCASE("Non-const span")
  {
    nonstd::span<uint8_t> span(bytes);
    CHECK(span.data() == bytes.data());
    CHECK(span.size() == bytes.size());
    span[1] = 'x';
    CHECK(bytes[1] == 'x');
  }
}

TEST_SUITE_END();
