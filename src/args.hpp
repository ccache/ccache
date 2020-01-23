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

#pragma once

#include "third_party/nonstd/optional.hpp"

class Args
{
public:
  Args() = default;
  Args(const Args& other);

  // operators for backwards compatibility
  Args& operator*() { return *this; }
  Args* operator->() { return this; }
  const Args* operator->() const { return this; }

  char** argv = nullptr;
  int argc = 0;
};

Args args_init(int, const char* const*);
Args args_init_from_string(const char*);
nonstd::optional<Args> args_init_from_gcc_atfile(const char* filename);
Args args_copy(const Args& args);

void args_free(Args& args);
void args_add(Args& args, const char* s);
void args_add_prefix(Args& args, const char* s);
void args_extend(Args& args, const Args& to_append);
void args_insert(Args& dest, int index, Args& src, bool replace);
void args_pop(Args& args, int n);
void args_set(Args& args, int index, const char* value);
void args_strip(Args& args, const char* prefix);
void args_remove_first(Args& args);
char* args_to_string(const Args& args);
bool args_equal(const Args& args1, const Args& args2);
