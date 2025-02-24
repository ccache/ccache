// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include "redisstorage.hpp"

#include <ccache/core/exceptions.hpp>
#include <ccache/hash.hpp>
#include <ccache/storage/storage.hpp>
#include <ccache/util/assertions.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/wincompat.hpp> // for timeval
#include <nonstd/span.hpp>

#ifdef HAVE_SYS_UTIME_H
#  include <sys/utime.h> // for timeval
#endif

// Ignore "ISO C++ forbids flexible array member ‘buf’" warning from -Wpedantic.
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpedantic"
#endif
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4200)
#endif
#include <sw/redis++/redis++.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

#include <cstdarg>
#include <map>
#include <memory>

namespace storage::remote {

namespace {

using namespace sw::redis;

const uint32_t DEFAULT_PORT = 6379;

class RedisStorageBackend : public RemoteStorage::Backend
{
public:
  RedisStorageBackend(const Url& url,
                      const std::vector<Backend::Attribute>& attributes);

  tl::expected<std::optional<util::Bytes>, Failure>
  get(const Hash::Digest& key) override;

  tl::expected<bool, Failure> put(const Hash::Digest& key,
                                  nonstd::span<const uint8_t> value,
                                  bool only_if_missing) override;

  tl::expected<bool, Failure> remove(const Hash::Digest& key) override;

private:
  std::string m_prefix;
  std::unique_ptr<RedisCluster> m_redis_cluster;
  std::unique_ptr<Redis> m_redis;
  bool is_cluster;

  std::string get_key_string(const Hash::Digest& digest) const;
};

timeval
to_timeval(const uint32_t ms)
{
  timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  return tv;
}

std::pair<std::optional<std::string>, std::optional<std::string>>
split_user_info(const std::string& user_info)
{
  const auto [left, right] = util::split_once(user_info, ':');
  if (left.empty()) {
    // redis://HOST
    return {std::nullopt, std::nullopt};
  } else if (right) {
    // redis://USERNAME:PASSWORD@HOST
    return {std::string(left), std::string(*right)};
  } else {
    // redis://PASSWORD@HOST
    return {std::nullopt, std::string(left)};
  }
}

RedisStorageBackend::RedisStorageBackend(
  const Url& url,
  const std::vector<Backend::Attribute>& attributes)
{
  ASSERT(url.scheme() == "redis" || url.scheme() == "redis+unix" || url.scheme() == "tcp");

  m_prefix = k_default_prefix;
  auto connect_timeout = k_default_connect_timeout;
  auto operation_timeout = k_default_operation_timeout;
  is_cluster = false;
  auto cluster_role = Role::MASTER;

  for (const auto& attr : attributes) {
    if (attr.key == "connect-timeout") {
      connect_timeout = parse_timeout_attribute(attr.value);
    } else if (attr.key == "operation-timeout") {
      operation_timeout = parse_timeout_attribute(attr.value);
    } else if (attr.key == "prefix") {
      m_prefix = attr.value;
    } else if (attr.key == "cluster") {
      is_cluster = util::to_lowercase(attr.value) == "true";
    } else if (attr.key == "cluster-role") {
      if (!is_cluster) {
        LOG("Cannot set cluster role {} since cluster is false", attr.value);
      } else if (util::to_lowercase(attr.value) == "slave") {
        cluster_role = Role::SLAVE;
      } else if (util::to_lowercase(attr.value) != "master") {
        LOG("Unkown cluster role: {}", attr.value);
      }
    } else if (!is_framework_attribute(attr.key)) {
      LOG("Unknown attribute: {}", attr.key);
    }
  }

  if (url.scheme() == "redis+unix" && !url.host().empty()
      && url.host() != "localhost") {
    throw core::Fatal(
      FMT("invalid file path \"{}\": specifying a host other than localhost is"
        " not supported",
      url.str(),
      url.host())
    );
  }

  if (url.scheme() == "redis+unix" && is_cluster) {
    throw core::Fatal("cannot connect to a redis cluster using a UNIX socket");
  }

  ConnectionOptions copts;
  if (url.scheme() == "redis+unix") {
    LOG("Redis connecting to {} (connect timeout {} ms)",
        url.path(),
        connect_timeout);
    copts.path = url.path();
  } else {
    const std::string host = url.host().empty() ? "localhost" : url.host();
    const uint32_t port =
      url.port().empty()
        ? DEFAULT_PORT
        : static_cast<uint32_t>(util::value_or_throw<core::Fatal>(
          util::parse_unsigned(url.port(), 1, 65535, "port")));
    ASSERT(url.path().empty() || url.path()[0] == '/');
    copts.host = host;
    copts.port = port;
    LOG("Redis connecting to {}:{} (connect timeout {} ms)",
        host,
        port,
        connect_timeout);
  }
  const auto [user, password] = split_user_info(url.user_info());
  if (password) {
    copts.password = *password;
    if (user) {
      // redis://user:password@host
      copts.user = *user;
      LOG("Redis AUTH {} {}", *user, storage::k_redacted_password);
    } else {
      // redis://password@host
      LOG("Redis AUTH {}", storage::k_redacted_password);
    }
  }
  std::optional<std::string> db;
  if (url.scheme() == "redis+unix") {
    for (const auto& param : url.query()) {
      if (param.key() == "db") {
        db = param.val();
        break;
      }
    }
  } else if (!url.path().empty()) {
    db = url.path().substr(1);
  }
  const uint32_t db_number =
    !db ? 0
        : static_cast<uint32_t>(
          util::value_or_throw<core::Fatal>(util::parse_unsigned(
            *db, 0, std::numeric_limits<uint32_t>::max(), "db number")));
  copts.db = db_number;
  if (db_number != 0) {
    LOG("Using database #{}", db_number);
  }
  copts.connect_timeout = connect_timeout;
  copts.socket_timeout = operation_timeout;
  if (is_cluster) {
    m_redis_cluster = std::make_unique<RedisCluster>(RedisCluster(copts, {}, cluster_role));
  } else {
    m_redis = std::make_unique<Redis>(Redis(copts));
  }
  LOG_RAW("Redis connection OK");
}

inline bool
is_error(int err)
{
  return err != REDIS_OK;
}

inline bool
is_timeout(int err)
{
#ifdef REDIS_ERR_TIMEOUT
  // Only returned for hiredis version 1.0.0 and above
  return err == REDIS_ERR_TIMEOUT;
#else
  (void)err;
  return false;
#endif
}

tl::expected<std::optional<util::Bytes>, RemoteStorage::Backend::Failure>
RedisStorageBackend::get(const Hash::Digest& key)
{
  const auto key_string = get_key_string(key);
  LOG("Redis GET {}", key_string);
  OptionalString val;
  if (is_cluster) {
    val = m_redis_cluster->get(key_string);
  } else {
    val = m_redis->get(key_string);
  }
  if (val) {
    return util::Bytes(util::to_span(std::string_view(*val)));
  } else {
    return std::nullopt;
  }
}

tl::expected<bool, RemoteStorage::Backend::Failure>
RedisStorageBackend::put(const Hash::Digest& key,
                         nonstd::span<const uint8_t> value,
                         bool only_if_missing)
{
  const auto key_string = get_key_string(key);
  if (only_if_missing) {
    LOG("Redis EXISTS {}", key_string);
    long long exists;
    if (is_cluster) {
      exists = m_redis_cluster->exists(key_string);
    } else {
      exists = m_redis->exists(key_string);
    }
    if (exists > 0) {
      LOG("Entry {} already in Redis", key_string);
      return false;
    }
  }
  LOG("Redis SET {} [{} bytes]", key_string, value.size());
  bool was_put;
  if (is_cluster) {
    was_put = m_redis_cluster->set(key_string, std::string(value.begin(), value.end()));
  } else {
    was_put = m_redis->set(key_string, std::string(value.begin(), value.end()));
  }
  return was_put;
}

tl::expected<bool, RemoteStorage::Backend::Failure>
RedisStorageBackend::remove(const Hash::Digest& key)
{
  const auto key_string = get_key_string(key);
  LOG("Redis DEL {}", key_string);
  long long deleted;
  if (is_cluster) {
    deleted = m_redis_cluster->del(key_string);
  } else {
    deleted = m_redis->del(key_string);
  }
  return deleted > 0;
}

std::string
RedisStorageBackend::get_key_string(const Hash::Digest& digest) const
{
  return FMT("{}:{}", m_prefix, util::format_digest(digest));
}

} // namespace

std::unique_ptr<RemoteStorage::Backend>
RedisStorage::create_backend(
  const Url& url, const std::vector<Backend::Attribute>& attributes) const
{
  return std::make_unique<RedisStorageBackend>(url, attributes);
}

} // namespace storage::remote
