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
#ifdef _MSC_VER
#  pragma warning(pop)
#endif
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

#include <cstdarg>
#include <memory>

namespace storage {
namespace secondary {

namespace {

using RedisContext = std::unique_ptr<redisContext, decltype(&redisFree)>;
using RedisReply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

const uint32_t DEFAULT_PORT = 6379;

class RedisStorageBackend : public SecondaryStorage::Backend
{
public:
  RedisStorageBackend(const SecondaryStorage::Backend::Params& params);

  nonstd::expected<nonstd::optional<std::string>, Failure>
  get(const Digest& key) override;

  nonstd::expected<bool, Failure> put(const Digest& key,
                                      const std::string& value,
                                      bool only_if_missing) override;

  nonstd::expected<bool, Failure> remove(const Digest& key) override;

private:
  const std::string m_prefix;
  RedisContext m_context;

  void
  connect(const Url& url, uint32_t connect_timeout, uint32_t operation_timeout);
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

std::pair<nonstd::optional<std::string>, nonstd::optional<std::string>>
split_user_info(const std::string& user_info)
{
  const auto pair = util::split_once(user_info, ':');
  if (pair.first.empty()) {
    // redis://HOST
    return {nonstd::nullopt, nonstd::nullopt};
  } else if (pair.second) {
    // redis://USERNAME:PASSWORD@HOST
    return {std::string(*pair.second), std::string(pair.first)};
  } else {
    // redis://PASSWORD@HOST
    return {std::string(pair.first), nonstd::nullopt};
  }
}

RedisStorageBackend::RedisStorageBackend(const Params& params)
  : m_prefix("ccache"), // TODO: attribute
    m_context(nullptr, redisFree)
{
  const auto& url = params.url;
  ASSERT(url.scheme() == "redis");

  auto connect_timeout = k_default_connect_timeout;
  auto operation_timeout = k_default_operation_timeout;

  for (const auto& attr : params.attributes) {
    if (attr.key == "connect-timeout") {
      connect_timeout = parse_timeout_attribute(attr.value);
    } else if (attr.key == "operation-timeout") {
      operation_timeout = parse_timeout_attribute(attr.value);
    } else if (!is_framework_attribute(attr.key)) {
      LOG("Unknown attribute: {}", attr.key);
    }
  }

  connect(url, connect_timeout.count(), operation_timeout.count());
  select_database(url);
  authenticate(url);
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

nonstd::expected<nonstd::optional<std::string>,
                 SecondaryStorage::Backend::Failure>
RedisStorageBackend::get(const Digest& key)
{
  const auto key_string = get_key_string(key);
  LOG("Redis GET {}", key_string);
  const auto reply = redis_command("GET %s", key_string.c_str());
  if (!reply) {
    return nonstd::make_unexpected(reply.error());
  } else if ((*reply)->type == REDIS_REPLY_STRING) {
    return std::string((*reply)->str, (*reply)->len);
  } else if ((*reply)->type == REDIS_REPLY_NIL) {
    return nonstd::nullopt;
  } else {
    LOG("Unknown reply type: {}", (*reply)->type);
    return nonstd::make_unexpected(Failure::error);
  }
}

nonstd::expected<bool, SecondaryStorage::Backend::Failure>
RedisStorageBackend::put(const Digest& key,
                         const std::string& value,
                         bool only_if_missing)
{
  const auto key_string = get_key_string(key);

  if (only_if_missing) {
    LOG("Redis EXISTS {}", key_string);
    const auto reply = redis_command("EXISTS %s", key_string.c_str());
    if (!reply) {
      return nonstd::make_unexpected(reply.error());
    } else if ((*reply)->type == REDIS_REPLY_INTEGER && (*reply)->integer > 0) {
      LOG("Entry {} already in Redis", key_string);
      return false;
    } else {
      LOG("Unknown reply type: {}", (*reply)->type);
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

nonstd::expected<bool, SecondaryStorage::Backend::Failure>
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

void
RedisStorageBackend::connect(const Url& url,
                             const uint32_t connect_timeout,
                             const uint32_t operation_timeout)
{
  const std::string host = url.host().empty() ? "localhost" : url.host();
  const uint32_t port = url.port().empty()
                          ? DEFAULT_PORT
                          : util::value_or_throw<core::Fatal>(
                            util::parse_unsigned(url.port(), 1, 65535, "port"));
  ASSERT(url.path().empty() || url.path()[0] == '/');

  LOG("Redis connecting to {}:{} (connect timeout {} ms)",
      url.host(),
      port,
      connect_timeout);
  m_context.reset(redisConnectWithTimeout(
    url.host().c_str(), port, to_timeval(connect_timeout)));

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
  const uint32_t db_number =
    url.path().empty() ? 0
                       : util::value_or_throw<core::Fatal>(util::parse_unsigned(
                         url.path().substr(1),
                         0,
                         std::numeric_limits<uint32_t>::max(),
                         "db number"));

  if (db_number != 0) {
    LOG("Redis SELECT {}", db_number);
    const auto reply =
      util::value_or_throw<Failed>(redis_command("SELECT %d", db_number));
  }
}

void
RedisStorageBackend::authenticate(const Url& url)
{
  const auto password_username_pair = split_user_info(url.user_info());
  const auto& password = password_username_pair.first;
  if (password) {
    decltype(redis_command("")) reply = nonstd::make_unexpected(Failure::error);
    const auto& username = password_username_pair.second;
    if (username) {
      LOG("Redis AUTH {} {}", *username, k_redacted_password);
      reply = util::value_or_throw<Failed>(
        redis_command("AUTH %s %s", username->c_str(), password->c_str()));
    } else {
      LOG("Redis AUTH {}", k_redacted_password);
      reply = util::value_or_throw<Failed>(
        redis_command("AUTH %s", password->c_str()));
    }
  }
}

nonstd::expected<RedisReply, SecondaryStorage::Backend::Failure>
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

std::unique_ptr<SecondaryStorage::Backend>
RedisStorage::create_backend(const Backend::Params& params) const
{
  return std::make_unique<RedisStorageBackend>(params);
}

void
RedisStorage::redact_secrets(Backend::Params& params) const
{
  auto& url = params.url;
  const auto user_info = util::split_once(url.user_info(), ':');
  if (user_info.second) {
    // redis://username:password@host
    url.user_info(FMT("{}:{}", user_info.first, k_redacted_password));
  } else if (!user_info.first.empty()) {
    // redis://password@host
    url.user_info(k_redacted_password);
  }
}

} // namespace secondary
} // namespace storage
