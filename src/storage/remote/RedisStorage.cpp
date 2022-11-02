// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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

#include "RedisStorage.hpp"

#include <Digest.hpp>
#include <Logging.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/expected.hpp>
#include <util/string.hpp>

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
#ifdef HAVE_REDISS_STORAGE_BACKEND
#  include <hiredis/hiredis_ssl.h>
#endif
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
#ifdef HAVE_REDISS_STORAGE_BACKEND
using RedisSSLContext =
  std::unique_ptr<redisSSLContext, decltype(&redisFreeSSLContext)>;
#endif
using RedisReply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

const uint32_t DEFAULT_PORT = 6379;

class RedisStorageBackend : public RemoteStorage::Backend
{
public:
  RedisStorageBackend(const RemoteStorage::Backend::Params& params);

  nonstd::expected<std::optional<util::Bytes>, Failure>
  get(const Digest& key) override;

  nonstd::expected<bool, Failure> put(const Digest& key,
                                      nonstd::span<const uint8_t> value,
                                      bool only_if_missing) override;

  nonstd::expected<bool, Failure> remove(const Digest& key) override;

private:
  const std::string m_prefix;
#ifdef HAVE_REDISS_STORAGE_BACKEND
  RedisSSLContext m_ssl_context;
#endif
  RedisContext m_context;

#ifdef HAVE_REDISS_STORAGE_BACKEND
  void init_ssl(const Url& url,
                std::optional<std::string> ca_cert,
                std::optional<std::string> cert,
                std::optional<std::string> key);
#endif
  void
  connect(const Url& url, uint32_t connect_timeout, uint32_t operation_timeout);
#ifdef HAVE_REDISS_STORAGE_BACKEND
  void initiate_ssl(const Url& url);
#endif
  void select_database(const Url& url);
  void authenticate(const Url& url);
  nonstd::expected<RedisReply, Failure> redis_command(const char* format, ...);
  std::string get_key_string(const Digest& digest) const;
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

#ifdef HAVE_REDISS_STORAGE_BACKEND
inline bool
is_secure(const Url& url)
{
  return url.scheme() == "rediss";
}
#endif

RedisStorageBackend::RedisStorageBackend(const Params& params)
  : m_prefix("ccache"), // TODO: attribute
#ifdef HAVE_REDISS_STORAGE_BACKEND
    m_ssl_context(nullptr, redisFreeSSLContext),
#endif
    m_context(nullptr, redisFree)
{
  const auto& url = params.url;
#ifdef HAVE_REDISS_STORAGE_BACKEND
  ASSERT(url.scheme() == "redis" || url.scheme() == "redis+unix"
         || url.scheme() == "rediss");
#else
  ASSERT(url.scheme() == "redis" || url.scheme() == "redis+unix");
#endif
  if (url.scheme() == "redis+unix" && !params.url.host().empty()
      && params.url.host() != "localhost") {
    throw core::Fatal(
      FMT("invalid file path \"{}\": specifying a host other than localhost is"
          " not supported",
          params.url.str(),
          params.url.host()));
  }

  std::optional<std::string> cacert;
  std::optional<std::string> cert;
  std::optional<std::string> key;
  auto connect_timeout = k_default_connect_timeout;
  auto operation_timeout = k_default_operation_timeout;

  for (const auto& attr : params.attributes) {
    if (attr.key == "cacert") {
      cacert = attr.value;
    } else if (attr.key == "cert") {
      cert = attr.value;
    } else if (attr.key == "key") {
      key = attr.value;
    } else if (attr.key == "connect-timeout") {
      connect_timeout = parse_timeout_attribute(attr.value);
    } else if (attr.key == "operation-timeout") {
      operation_timeout = parse_timeout_attribute(attr.value);
    } else if (!is_framework_attribute(attr.key)) {
      LOG("Unknown attribute: {}", attr.key);
    }
  }

#ifdef HAVE_REDISS_STORAGE_BACKEND
  init_ssl(url, cacert, cert, key);
#endif
  connect(url, connect_timeout.count(), operation_timeout.count());
#ifdef HAVE_REDISS_STORAGE_BACKEND
  initiate_ssl(url);
#endif
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

nonstd::expected<std::optional<util::Bytes>, RemoteStorage::Backend::Failure>
RedisStorageBackend::get(const Digest& key)
{
  const auto key_string = get_key_string(key);
  LOG("Redis GET {}", key_string);
  const auto reply = redis_command("GET %s", key_string.c_str());
  if (!reply) {
    return nonstd::make_unexpected(reply.error());
  } else if ((*reply)->type == REDIS_REPLY_STRING) {
    return util::Bytes((*reply)->str, (*reply)->len);
  } else if ((*reply)->type == REDIS_REPLY_NIL) {
    return std::nullopt;
  } else {
    LOG("Unknown reply type: {}", (*reply)->type);
    return nonstd::make_unexpected(Failure::error);
  }
}

nonstd::expected<bool, RemoteStorage::Backend::Failure>
RedisStorageBackend::put(const Digest& key,
                         nonstd::span<const uint8_t> value,
                         bool only_if_missing)
{
  const auto key_string = get_key_string(key);

  if (only_if_missing) {
    LOG("Redis EXISTS {}", key_string);
    const auto reply = redis_command("EXISTS %s", key_string.c_str());
    if (!reply) {
      return nonstd::make_unexpected(reply.error());
    } else if ((*reply)->type != REDIS_REPLY_INTEGER) {
      LOG("Unknown reply type: {}", (*reply)->type);
    } else if ((*reply)->integer > 0) {
      LOG("Entry {} already in Redis", key_string);
      return false;
    }
  }

  LOG("Redis SET {} [{} bytes]", key_string, value.size());
  const auto reply =
    redis_command("SET %s %b", key_string.c_str(), value.data(), value.size());
  if (!reply) {
    return nonstd::make_unexpected(reply.error());
  } else if ((*reply)->type == REDIS_REPLY_STATUS) {
    return true;
  } else {
    LOG("Unknown reply type: {}", (*reply)->type);
    return nonstd::make_unexpected(Failure::error);
  }
}

nonstd::expected<bool, RemoteStorage::Backend::Failure>
RedisStorageBackend::remove(const Digest& key)
{
  const auto key_string = get_key_string(key);
  LOG("Redis DEL {}", key_string);
  const auto reply = redis_command("DEL %s", key_string.c_str());
  if (!reply) {
    return nonstd::make_unexpected(reply.error());
  } else if ((*reply)->type == REDIS_REPLY_INTEGER) {
    return (*reply)->integer > 0;
  } else {
    LOG("Unknown reply type: {}", (*reply)->type);
    return nonstd::make_unexpected(Failure::error);
  }
}

#ifdef HAVE_REDISS_STORAGE_BACKEND
void
RedisStorageBackend::init_ssl(const Url& url,
                              std::optional<std::string> ca_cert,
                              std::optional<std::string> cert,
                              std::optional<std::string> key)
{
  if (is_secure(url)) {
    if (redisInitOpenSSL() != REDIS_OK) {
      throw Failed("Redis SSL init OpenSSL failed");
    }
    redisSSLContextError ssl_error;
    m_ssl_context.reset(
      redisCreateSSLContext((ca_cert ? ca_cert->c_str() : NULL),
                            NULL,
                            (cert ? cert->c_str() : NULL),
                            (key ? key->c_str() : NULL),
                            NULL,
                            &ssl_error));
    if (!m_ssl_context) {
      throw Failed(FMT("Redis context construction error: {}",
                       redisSSLContextGetError(ssl_error)));
    }
  }
}
#endif

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
      url.port().empty() ? DEFAULT_PORT
                         : util::value_or_throw<core::Fatal>(
                           util::parse_unsigned(url.port(), 1, 65535, "port"));
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

#ifdef HAVE_REDISS_STORAGE_BACKEND
void
RedisStorageBackend::initiate_ssl(const Url& url)
{
  if (is_secure(url)) {
    if (redisInitiateSSLWithContext(m_context.get(), m_ssl_context.get())
        != REDIS_OK) {
      throw Failed("Failed to initiate ssl");
    }
  }
}
#endif

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
        : util::value_or_throw<core::Fatal>(util::parse_unsigned(
          *db, 0, std::numeric_limits<uint32_t>::max(), "db number"));

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
      LOG("Redis AUTH {} {}", *user, k_redacted_password);
      util::value_or_throw<Failed>(
        redis_command("AUTH %s %s", user->c_str(), password->c_str()));
    } else {
      // redis://password@host
      LOG("Redis AUTH {}", k_redacted_password);
      util::value_or_throw<Failed>(redis_command("AUTH %s", password->c_str()));
    }
  }
}

nonstd::expected<RedisReply, RemoteStorage::Backend::Failure>
RedisStorageBackend::redis_command(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  auto reply =
    static_cast<redisReply*>(redisvCommand(m_context.get(), format, ap));
  va_end(ap);
  if (!reply) {
    LOG("Redis command failed: {}", m_context->errstr);
    return nonstd::make_unexpected(is_timeout(m_context->err) ? Failure::timeout
                                                              : Failure::error);
  } else if (reply->type == REDIS_REPLY_ERROR) {
    LOG("Redis command failed: {}", reply->str);
    return nonstd::make_unexpected(Failure::error);
  } else {
    return RedisReply(reply, freeReplyObject);
  }
}

std::string
RedisStorageBackend::get_key_string(const Digest& digest) const
{
  return FMT("{}:{}", m_prefix, digest.to_string());
}

} // namespace

std::unique_ptr<RemoteStorage::Backend>
RedisStorage::create_backend(const Backend::Params& params) const
{
  return std::make_unique<RedisStorageBackend>(params);
}

void
RedisStorage::redact_secrets(Backend::Params& params) const
{
  auto& url = params.url;
  const auto [user, password] = split_user_info(url.user_info());
  if (password) {
    if (user) {
      // redis://user:password@host
      url.user_info(FMT("{}:{}", *user, k_redacted_password));
    } else {
      // redis://password@host
      url.user_info(k_redacted_password);
    }
  }
}

} // namespace storage::remote
