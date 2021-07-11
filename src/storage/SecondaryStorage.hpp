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

#include <third_party/nonstd/expected.hpp>
#include <third_party/nonstd/optional.hpp>

#include <string>

class Digest;

namespace storage {

constexpr auto k_masked_password = "********";

// This class defines the API that a secondary storage backend must implement.
class SecondaryStorage
{
public:
  enum class Error {
    error,   // Operation error, e.g. failed connection or authentication.
    timeout, // Timeout, e.g. due to slow network or server.
  };

  virtual ~SecondaryStorage() = default;

  // Get the value associated with `key`. Returns the value on success or
  // nonstd::nullopt if the entry is not present.
  virtual nonstd::expected<nonstd::optional<std::string>, Error>
  get(const Digest& key) = 0;

  // Put `value` associated to `key` in the storage. A true `only_if_missing` is
  // a hint that the value does not have to be set if already present. Returns
  // true if the entry was stored, otherwise false.
  virtual nonstd::expected<bool, Error> put(const Digest& key,
                                            const std::string& value,
                                            bool only_if_missing = false) = 0;

  // Remove `key` and its associated value. Returns true if the entry was
  // removed, otherwise false.
  virtual nonstd::expected<bool, Error> remove(const Digest& key) = 0;
};

} // namespace storage
