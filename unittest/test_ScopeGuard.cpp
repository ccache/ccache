// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "NonCopyable.hpp"
#include "ScopeGuard.hpp"

#include "third_party/catch.hpp"

struct ptr_deleter
{
  void
  operator()(int*& ptr)
  {
    delete ptr;
    ptr = nullptr;
  }
};

TEST_CASE("delete pointer")
{
  using ScopeGuardInt = ScopeGuard<ptr_deleter, int*>;
  int* i = new int(3);

  {
    ScopeGuardInt guard(i);
    CHECK(*i == 3);
  }

  CHECK(i == nullptr);
}

struct Value : NonCopyable
{
  int i = 3;
};

struct reset_value
{
  void
  operator()(Value& v)
  {
    v.i = 0;
  }
};

TEST_CASE("reset a value type")
{
  using ScopeGuardValue = ScopeGuard<reset_value, Value>;

  Value v;

  CHECK(v.i == 3);

  {
    ScopeGuardValue guard(v);
    CHECK(v.i == 3);
  }

  CHECK(v.i == 0);
}
