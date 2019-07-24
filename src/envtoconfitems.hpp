// Copyright (C) 2018-2019 Joel Rosdahl and other contributors
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

#ifndef ENVTOCONFITEMS_H
#define ENVTOCONFITEMS_H

#include "system.hpp"

struct env_to_conf_item {
	const char *env_name;
	const char *conf_name;
};

const struct env_to_conf_item *envtoconfitems_get(const char *str, size_t len);

size_t envtoconfitems_count(void);

#endif
