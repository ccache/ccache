// Copyright (C) 2023 Joel Rosdahl and other contributors
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

#include <type_traits>

namespace util {

template<typename T> class BitSet
{
public:
  explicit BitSet(T value = {});
  BitSet(const BitSet& set);

  BitSet& operator=(const BitSet& set);

  bool contains(T value) const;
  bool empty() const;

  void clear();
  void insert(T value);
  void insert(const BitSet& set);
  void erase(T value);

  template<typename U> static BitSet<T> from_bitmask(U mask);
  typename std::underlying_type<T>::type to_bitmask() const;

private:
  typename std::underlying_type<T>::type m_value;
};

// --- Inline implementations ---

template<typename T>
inline BitSet<T>::BitSet(T value)
  : m_value(static_cast<typename std::underlying_type<T>::type>(value))
{
}

template<typename T>
inline BitSet<T>::BitSet(const BitSet& set)
  : m_value(set.m_value)
{
}

template<typename T>
inline BitSet<T>&
BitSet<T>::operator=(const BitSet& set)
{
  m_value = set.m_value;
  return *this;
}

template<typename T>
inline bool
BitSet<T>::contains(T value) const
{
  return m_value & static_cast<typename std::underlying_type<T>::type>(value);
}

template<typename T>
inline bool
BitSet<T>::empty() const
{
  return to_bitmask() == 0;
}

template<typename T>
inline void
BitSet<T>::insert(T value)
{
  m_value |= static_cast<typename std::underlying_type<T>::type>(value);
}

template<typename T>
inline void
BitSet<T>::insert(const BitSet& set)
{
  m_value |= static_cast<typename std::underlying_type<T>::type>(set.m_value);
}

template<typename T>
inline void
BitSet<T>::erase(T value)
{
  m_value &= ~static_cast<typename std::underlying_type<T>::type>(value);
}

template<typename T>
template<typename U>
inline BitSet<T>
BitSet<T>::from_bitmask(U mask)
{
  BitSet result;
  result.m_value = mask;
  return result;
}

template<typename T>
inline typename std::underlying_type<T>::type
BitSet<T>::to_bitmask() const
{
  return m_value;
}

} // namespace util
