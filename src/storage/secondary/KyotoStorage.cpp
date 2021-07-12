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

#include "KyotoStorage.hpp"

#include <Digest.hpp>
#include <Logging.hpp>
#include <fmtmacros.hpp>

#include <ktremotedb.h>
using namespace kyototycoon;

namespace storage {
namespace secondary {

KyotoStorage::KyotoStorage(const Url& url, const AttributeMap& /*attributesi*/)
  : m_url(url)
{
  m_db = new RemoteDB();
  m_opened = false;
  m_invalid = false;
}

KyotoStorage::~KyotoStorage()
{
  close();
  if (m_db) {
    delete m_db;
    m_db = nullptr;
  }
}

bool
KyotoStorage::open()
{
  if (m_opened) {
    return true;
  }
  if (m_invalid) {
    return false;
  }

  ASSERT(m_url.scheme() == "kt");
  std::string host = m_url.host();
  std::string port = m_url.port();
  double timeout = -1; // TODO
  bool ok;
  if (!host.empty()) {
    int p = port.empty() ? 1978 : std::stoi(port);
    ok = m_db->open(host, p, timeout);
  } else {
    LOG("Kyoto invalid url: {}", m_url.str());
    m_invalid = true;
    return false;
  }

  if (!ok) {
    LOG("Kyoto open {} err: {}", m_url.str(), m_db->error().name());
    m_invalid = true;
    return false;
  } else {
    LOG("Kyoto open {} OK", m_url.str());
    m_opened = true;
    return true;
  }
}

void
KyotoStorage::close()
{
  if (m_opened) {
    bool ok = m_db->close();
    if (!ok) {
      LOG("Kyoto close err: {}", m_db->error().name());
    } else {
      LOG_RAW("Kyoto close OK");
    }
    m_opened = false;
  }
}

nonstd::expected<nonstd::optional<std::string>, SecondaryStorage::Error>
KyotoStorage::get(const Digest& key)
{
  bool ok = open();
  if (!ok) {
    return nonstd::make_unexpected(Error::error);
  }
  const std::string key_string = get_key_string(key);
  LOG("Kyoto get {}", key_string);
  bool found = false;
  bool missing = false;
  std::string value;
  found = m_db->get(key_string, &value);
  if (!found && m_db->error().code() == RemoteDB::Error::LOGIC) {
    missing = true;
  } else if (!found) {
    LOG("Failed to get {} from kt: {}", key_string, m_db->error().name());
  }
  if (found) {
    return value;
  } else if (missing) {
    return nonstd::nullopt;
  } else {
    return nonstd::make_unexpected(Error::error);
  }
}

nonstd::expected<bool, SecondaryStorage::Error>
KyotoStorage::put(const Digest& key,
                  const std::string& value,
                  bool only_if_missing)
{
  bool ok = open();
  if (!ok) {
    return nonstd::make_unexpected(Error::error);
  }
  const std::string key_string = get_key_string(key);
  if (only_if_missing) {
    LOG("Kyoto check {}", key_string);
    auto size = m_db->check(key_string);
    if (size > 0) {
      return false;
    }
  }
  LOG("Kyoto set {}", key_string);
  bool stored = m_db->set(key_string, value);
  if (!stored) {
    LOG("Failed to set {} to kt: {}", key_string, m_db->error().name());
  }
  if (stored) {
    return true;
  } else {
    return nonstd::make_unexpected(Error::error);
  }
}

nonstd::expected<bool, SecondaryStorage::Error>
KyotoStorage::remove(const Digest& key)
{
  bool ok = open();
  if (!ok) {
    return nonstd::make_unexpected(Error::error);
  }
  const std::string key_string = get_key_string(key);
  LOG("Kyoto remove {}", key_string);
  bool removed = m_db->remove(key_string);
  if (!removed) {
    LOG("Failed to remove {} in kt: {}", key_string, m_db->error().name());
  }
  if (removed) {
    return true;
  } else {
    return nonstd::make_unexpected(Error::error);
  }
}

std::string
KyotoStorage::get_key_string(const Digest& digest) const
{
  return digest.to_string();
}

} // namespace secondary
} // namespace storage
