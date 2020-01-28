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

#include "src/ScopeGuard.hpp"

struct args
{
  char** argv;
  int argc;
};

struct args* args_init(int, const char* const*);
struct args* args_init_from_string(const char*);
struct args* args_init_from_gcc_atfile(const char* filename);
struct args* args_copy(struct args* args);
void args_free(struct args* args);
void args_add(struct args* args, const char* s);
void args_add_prefix(struct args* args, const char* s);
void args_extend(struct args* args, struct args* to_append);
void args_insert(struct args* dest, int index, struct args* src, bool replace);
void args_pop(struct args* args, int n);
void args_set(struct args* args, int index, const char* value);
void args_strip(struct args* args, const char* prefix);
void args_remove_first(struct args* args);
char* args_to_string(const struct args* args);
bool args_equal(const struct args* args1, const struct args* args2);

struct args_deleter
{
  void operator()(struct args*& arg);
};
using ArgsScopeGuard = ScopeGuard<args_deleter, struct args*>;
