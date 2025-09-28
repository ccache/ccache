// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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
#include <hiredis/hiredis.h>
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

using RedisContext = std::unique_ptr<redisContext, decltype(&redisFree)>;
using RedisReply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

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
                                  Overwrite overwrite) override;

  tl::expected<bool, Failure> remove(const Hash::Digest& key) override;

private:
  std::string m_prefix;
  RedisContext m_context;

  void
  connect(const Url& url, uint32_t connect_timeout, uint32_t operation_timeout);
  void select_database(const Url& url);
  void authenticate(const Url& url);
  tl::expected<RedisReply, Failure> redis_command(const char* format, ...);
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
  const auto [left, right] = util::split_once_into_views(user_info, ':');
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
  : m_prefix("ccache"), // TODO: attribute
    m_context(nullptr, redisFree)
{
  ASSERT(url.scheme() == "redis" || url.scheme() == "redis+unix");
  if (url.scheme() == "redis+unix" && !url.host().empty()
      && url.host() != "localhost") {
    throw core::Fatal(
      FMT("invalid file path \"{}\": specifying a host other than localhost is"
          " not supported",
          url.str(),
          url.host()));
  }

  auto connect_timeout = k_default_connect_timeout;
  auto operation_timeout = k_default_operation_timeout;

  for (const auto& attr : attributes) {
    if (attr.key == "connect-timeout") {
      connect_timeout = parse_timeout_attribute(attr.value);
    } else if (attr.key == "operation-timeout") {
      operation_timeout = parse_timeout_attribute(attr.value);
    } else if (!is_framework_attribute(attr.key)) {
      LOG("Unknown attribute: {}", attr.key);
    }
  }

  connect(url,
          static_cast<uint32_t>(connect_timeout.count()),
          static_cast<uint32_t>(operation_timeout.count()));
  authenticate(url);
  select_database(url);
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
  TRY_ASSIGN(const auto reply, redis_command("GET %s", key_string.c_str()));
  if (reply->type == REDIS_REPLY_STRING) {
    return util::Bytes(reply->str, reply->len);
  } else if (reply->type == REDIS_REPLY_NIL) {
    return std::nullopt;
  } else {
    LOG("Unknown reply type: {}", reply->type);
    return tl::unexpected(Failure::error);
  }
}

tl::expected<bool, RemoteStorage::Backend::Failure>
RedisStorageBackend::put(const Hash::Digest& key,
                         nonstd::span<const uint8_t> value,
                         Overwrite overwrite)
{
  const auto key_string = get_key_string(key);

  if (overwrite == Overwrite::no) {
    LOG("Redis EXISTS {}", key_string);
    TRY_ASSIGN(const auto reply,
               redis_command("EXISTS %s", key_string.c_str()));
    if (reply->type != REDIS_REPLY_INTEGER) {
      LOG("Unknown reply type: {}", reply->type);
    } else if (reply->integer > 0) {
      LOG("Entry {} already in Redis", key_string);
      return false;
    }
  }

  LOG("Redis SET {} [{} bytes]", key_string, value.size());
  TRY_ASSIGN(
    const auto reply,
    redis_command("SET %s %b", key_string.c_str(), value.data(), value.size()));
  if (reply->type == REDIS_REPLY_STATUS) {
    return true;
  } else {
    LOG("Unknown reply type: {}", reply->type);
    return tl::unexpected(Failure::error);
  }
}

tl::expected<bool, RemoteStorage::Backend::Failure>
RedisStorageBackend::remove(const Hash::Digest& key)
{
  const auto key_string = get_key_string(key);
  LOG("Redis DEL {}", key_string);
  TRY_ASSIGN(const auto reply, redis_command("DEL %s", key_string.c_str()));
  if (reply->type == REDIS_REPLY_INTEGER) {
    return reply->integer > 0;
  } else {
    LOG("Unknown reply type: {}", reply->type);
    return tl::unexpected(Failure::error);
  }
}

void
RedisStorageBackend::connect(const Url& url,
                             const uint32_t connect_timeout,
                             const uint32_t operation_timeout)
{
  if (url.scheme() == "redis+unix") {
    LOG("Redis connecting to {} (connect timeout {} ms)",
        url.path(),
        connect_timeout);
    m_context.reset(redisConnectUnixWithTimeout(url.path().c_str(),
                                                to_timeval(connect_timeout)));
  } else {
    const std::string host = url.host().empty() ? "localhost" : url.host();
    const uint32_t port =
      url.port().empty()
        ? DEFAULT_PORT
        : static_cast<uint32_t>(util::value_or_throw<core::Fatal>(
            util::parse_unsigned(url.port(), 1, 65535, "port")));
    ASSERT(url.path().empty() || url.path()[0] == '/');

    LOG("Redis connecting to {}:{} (connect timeout {} ms)",
        host,
        port,
        connect_timeout);
    m_context.reset(
      redisConnectWithTimeout(host.c_str(), port, to_timeval(connect_timeout)));
  }

  if (!m_context) {
    throw Failed("Redis context construction error");
  }
  if (is_timeout(m_context->err)) {
    throw Failed(FMT("Redis connection timeout: {}", m_context->errstr),
                 Failure::timeout);
  }
  if (is_error(m_context->err)) {
    throw Failed(FMT("Redis connection error: {}", m_context->errstr));
  }

  LOG("Redis operation timeout set to {} ms", operation_timeout);
  if (redisSetTimeout(m_context.get(), to_timeval(operation_timeout))
      != REDIS_OK) {
    throw Failed("Failed to set operation timeout");
  }

  LOG_RAW("Redis connection OK");
}

void
RedisStorageBackend::select_database(const Url& url)
{
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

  if (db_number != 0) {
    LOG("Redis SELECT {}", db_number);
    util::value_or_throw<Failed>(redis_command("SELECT %d", db_number));
  }
}

void
RedisStorageBackend::authenticate(const Url& url)
{
  const auto [user, password] = split_user_info(url.user_info());
  if (password) {
    if (user) {
      // redis://user:password@host
      LOG("Redis AUTH {} {}", *user, storage::k_redacted_password);
      util::value_or_throw<Failed>(
        redis_command("AUTH %s %s", user->c_str(), password->c_str()));
    } else {
      // redis://password@host
      LOG("Redis AUTH {}", storage::k_redacted_password);
      util::value_or_throw<Failed>(redis_command("AUTH %s", password->c_str()));
    }
  }
}

tl::expected<RedisReply, RemoteStorage::Backend::Failure>
RedisStorageBackend::redis_command(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  auto reply =
    static_cast<redisReply*>(redisvCommand(m_context.get(), format, ap));
  va_end(ap);
  if (!reply) {
    LOG("Redis command failed: {}", m_context->errstr);
    return tl::unexpected(is_timeout(m_context->err) ? Failure::timeout
                                                     : Failure::error);
  } else if (reply->type == REDIS_REPLY_ERROR) {
    LOG("Redis command failed: {}", reply->str);
    return tl::unexpected(Failure::error);
  } else {
    return RedisReply(reply, freeReplyObject);
  }
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
