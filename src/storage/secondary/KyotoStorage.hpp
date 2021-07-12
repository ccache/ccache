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

#include "storage/SecondaryStorage.hpp"
#include "storage/types.hpp"

#include <third_party/url.hpp>

namespace kyototycoon {
class RemoteDB;
}

namespace storage {
namespace secondary {

class KyotoStorage : public storage::SecondaryStorage
{
public:
  KyotoStorage(const Url& url, const AttributeMap& attributes);
  ~KyotoStorage();

  nonstd::expected<nonstd::optional<std::string>, Error>
  get(const Digest& key) override;
  nonstd::expected<bool, Error> put(const Digest& key,
                                    const std::string& value,
                                    bool only_if_missing) override;
  nonstd::expected<bool, Error> remove(const Digest& key) override;

private:
  Url m_url;
  kyototycoon::RemoteDB* m_db;
  bool m_opened;
  bool m_invalid;

  bool open();
  void close();
  std::string get_key_string(const Digest& digest) const;
};

} // namespace secondary
} // namespace storage
