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

#include <storage/SecondaryStorage.hpp>
#include <storage/types.hpp>

#include <memory>
#include <string>

class Url;

namespace httplib {
class Client;
}

namespace storage {
namespace secondary {

class HttpStorage : public SecondaryStorage
{
public:
  HttpStorage(const Url& url, const AttributeMap& attributes);
  ~HttpStorage() override;

  nonstd::expected<nonstd::optional<std::string>, Error>
  get(const Digest& key) override;
  nonstd::expected<bool, Error> put(const Digest& key,
                                    const std::string& value,
                                    bool only_if_missing) override;
  nonstd::expected<bool, Error> remove(const Digest& key) override;

private:
  const std::string m_url_path;
  std::unique_ptr<httplib::Client> m_http_client;

  std::string get_entry_path(const Digest& key) const;
};

} // namespace secondary
} // namespace storage
