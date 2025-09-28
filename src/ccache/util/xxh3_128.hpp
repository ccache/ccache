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

#pragma once

#include <ccache/util/bytes.hpp>
#include <ccache/util/conversion.hpp>

#include <nonstd/span.hpp>
#ifdef USE_XXH_DISPATCH
#  include <xxh_x86dispatch.h>
#else
#  include <xxhash.h>
#endif

#include <cstdint>
#include <cstring>

namespace util {

class XXH3_128
{
public:
  static constexpr size_t k_digest_size = 16;

  XXH3_128();
  ~XXH3_128();

  void reset();
  void update(nonstd::span<const uint8_t> data);
  Bytes digest() const;

private:
  XXH3_state_t* m_state;
};

inline XXH3_128::XXH3_128()
  : m_state(XXH3_createState())
{
  reset();
}

inline XXH3_128::~XXH3_128()
{
  XXH3_freeState(m_state);
}

inline void
XXH3_128::reset()
{
  XXH3_128bits_reset(m_state);
}

inline void
XXH3_128::update(nonstd::span<const uint8_t> data)
{
  XXH3_128bits_update(m_state, data.data(), data.size());
}

inline Bytes
XXH3_128::digest() const
{
  const auto result = XXH3_128bits_digest(m_state);
  Bytes digest(k_digest_size);
  int_to_big_endian(result.high64, &digest[0]);
  int_to_big_endian(result.low64, &digest[8]);
  return digest;
}

} // namespace util
