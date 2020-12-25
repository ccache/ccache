// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "core/system.hpp"

#ifdef USE_XXH_DISPATCH
#  include "third_party/xxh_x86dispatch.h"
#else
#  include "third_party/xxhash.h"
#endif

class Checksum
{
public:
  Checksum();
  ~Checksum();

  void reset();
  void update(const void* data, size_t length);
  uint64_t digest() const;

private:
  XXH3_state_t* m_state;
};

inline Checksum::Checksum() : m_state(XXH3_createState())
{
  reset();
}

inline Checksum::~Checksum()
{
  XXH3_freeState(m_state);
}

inline void
Checksum::reset()
{
  XXH3_64bits_reset(m_state);
}

inline void
Checksum::update(const void* data, size_t length)
{
  XXH3_64bits_update(m_state, data, length);
}

inline uint64_t
Checksum::digest() const
{
  return XXH3_64bits_digest(m_state);
}
