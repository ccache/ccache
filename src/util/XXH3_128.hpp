// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#ifdef USE_XXH_DISPATCH
#  include "third_party/xxh_x86dispatch.h"
#else
#  include "third_party/xxhash.h"
#endif

#include <Util.hpp>

#include <cstdint>
#include <cstring>

namespace util {

class XXH3_128
{
public:
  struct Digest
  {
  public:
    const uint8_t* bytes() const;
    uint8_t* bytes();
    constexpr static size_t size();

    bool operator==(const Digest& other) const;
    bool operator!=(const Digest& other) const;

  private:
    uint8_t m_bytes[16] = {};
  };

  XXH3_128();
  ~XXH3_128();

  void reset();
  void update(const void* data, size_t length);
  Digest digest() const;

private:
  XXH3_state_t* m_state;
};

inline const uint8_t*
XXH3_128::Digest::bytes() const
{
  return m_bytes;
}

inline uint8_t*
XXH3_128::Digest::bytes()
{
  return m_bytes;
}

inline constexpr size_t
XXH3_128::Digest::size()
{
  return sizeof(m_bytes);
}

inline bool
XXH3_128::Digest::operator==(const XXH3_128::Digest& other) const
{
  return memcmp(bytes(), other.bytes(), size()) == 0;
}

inline bool
XXH3_128::Digest::operator!=(const XXH3_128::Digest& other) const
{
  return !(*this == other);
}

inline XXH3_128::XXH3_128() : m_state(XXH3_createState())
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
XXH3_128::update(const void* data, size_t length)
{
  XXH3_128bits_update(m_state, data, length);
}

inline XXH3_128::Digest
XXH3_128::digest() const
{
  const auto result = XXH3_128bits_digest(m_state);
  XXH3_128::Digest digest;
  Util::int_to_big_endian(result.high64, digest.bytes());
  Util::int_to_big_endian(result.low64, digest.bytes() + 8);
  return digest;
}

} // namespace util
