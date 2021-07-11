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
#include <fmtmacros.hpp>
#include <util/string_utils.hpp>

#include <hiredis/hiredis.h>

#include <cstdarg>
#include <memory>

namespace storage {
namespace secondary {

const uint64_t DEFAULT_CONNECT_TIMEOUT_MS = 100;
const uint64_t DEFAULT_OPERATION_TIMEOUT_MS = 10000;
const uint32_t DEFAULT_PORT = 6379;

using RedisReply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

static RedisReply
redis_command(redisContext* context, const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  void* reply = redisvCommand(context, format, ap);
  va_end(ap);
  return RedisReply(static_cast<redisReply*>(reply), freeReplyObject);
}

static struct timeval
milliseconds_to_timeval(const uint64_t ms)
{
  struct timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  return tv;
}

static uint64_t
parse_timeout_attribute(const AttributeMap& attributes,
                        const std::string& name,
                        const uint64_t default_value)
{
  const auto it = attributes.find(name);
  if (it == attributes.end()) {
    return default_value;
  } else {
    return Util::parse_unsigned(it->second, 1, 1000 * 3600, "timeout");
  }
}

static std::pair<nonstd::optional<std::string>, nonstd::optional<std::string>>
split_user_info(const std::string& user_info)
{
  const auto pair = util::split_once(user_info, ':');
  if (pair.first.empty()) {
    // redis://HOST
    return {nonstd::nullopt, nonstd::nullopt};
  } else if (pair.second) {
    // redis://USERNAME:PASSWORD@HOST
    return {to_string(*pair.second), to_string(pair.first)};
  } else {
    // redis://PASSWORD@HOST
    return {to_string(pair.first), nonstd::nullopt};
  }
}

RedisStorage::RedisStorage(const Url& url, const AttributeMap& attributes)
  : m_url(url),
    m_prefix("ccache"), // TODO: attribute
    m_context(nullptr),
    m_connect_timeout(parse_timeout_attribute(
      attributes, "connect-timeout", DEFAULT_CONNECT_TIMEOUT_MS)),
    m_operation_timeout(parse_timeout_attribute(
      attributes, "operation-timeout", DEFAULT_OPERATION_TIMEOUT_MS)),
    m_connected(false),
    m_invalid(false)
{
}

RedisStorage::~RedisStorage()
{
  if (m_context) {
    LOG_RAW("Redis disconnect");
    redisFree(m_context);
    m_context = nullptr;
  }
}

int
RedisStorage::connect()
{
  if (m_connected) {
    return REDIS_OK;
  }
  if (m_invalid) {
    return REDIS_ERR;
  }

  if (m_context) {
    if (redisReconnect(m_context) == REDIS_OK) {
      m_connected = true;
      return REDIS_OK;
    }
    LOG("Redis reconnection error: {}", m_context->errstr);
    redisFree(m_context);
    m_context = nullptr;
  }

  ASSERT(m_url.scheme() == "redis");
  const std::string host = m_url.host().empty() ? "localhost" : m_url.host();
  const uint32_t port =
    m_url.port().empty() ? DEFAULT_PORT
                         : Util::parse_unsigned(m_url.port(), 1, 65535, "port");
  ASSERT(m_url.path().empty() || m_url.path()[0] == '/');
  const uint32_t db_number =
    m_url.path().empty()
      ? 0
      : Util::parse_unsigned(m_url.path().substr(1),
                             0,
                             std::numeric_limits<uint32_t>::max(),
                             "db number");

  const auto connect_timeout = milliseconds_to_timeval(m_connect_timeout);

  LOG("Redis connecting to {}:{} (timeout {} ms)",
      host.c_str(),
      port,
      m_connect_timeout);
  m_context = redisConnectWithTimeout(host.c_str(), port, connect_timeout);

  if (!m_context) {
    LOG_RAW("Redis connection error (NULL context)");
    m_invalid = true;
    return REDIS_ERR;
  } else if (m_context->err) {
    LOG("Redis connection error: {}", m_context->errstr);
    m_invalid = true;
    return m_context->err;
  }

  LOG("Redis connection to {}:{} OK", m_context->tcp.host, m_context->tcp.port);
  m_connected = true;

  if (redisSetTimeout(m_context, milliseconds_to_timeval(m_operation_timeout))
      != REDIS_OK) {
    LOG_RAW("Failed to set operation timeout");
  }

  if (db_number != 0) {
    LOG("Redis SELECT {}", db_number);
    const auto reply = redis_command(m_context, "SELECT %d", db_number);
    if (!reply) {
      LOG_RAW("Redis SELECT failed (NULL)");
      m_invalid = true;
      return REDIS_ERR;
    } else if (reply->type == REDIS_REPLY_ERROR) {
      LOG("Redis SELECT error: {}", reply->str);
      m_invalid = true;
      return REDIS_ERR;
    }
  }

  return auth();
}

int
RedisStorage::auth()
{
  const auto password_username_pair = split_user_info(m_url.user_info());
  const auto& password = password_username_pair.first;
  if (password) {
    RedisReply reply(nullptr, freeReplyObject);
    const auto& username = password_username_pair.second;
    if (username) {
      LOG("Redis AUTH {} {}", *username, storage::k_masked_password);
      reply = redis_command(
        m_context, "AUTH %s %s", username->c_str(), password->c_str());
    } else {
      LOG("Redis AUTH {}", storage::k_masked_password);
      reply = redis_command(m_context, "AUTH %s", password->c_str());
    }
    if (!reply) {
      LOG_RAW("Redis AUTH failed (NULL)");
      m_invalid = true;
      return REDIS_ERR;
    } else if (reply->type == REDIS_REPLY_ERROR) {
      LOG("Redis AUTH error: {}", reply->str);
      m_invalid = true;
      return REDIS_ERR;
    }
  }

  return REDIS_OK;
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

nonstd::expected<nonstd::optional<std::string>, SecondaryStorage::Error>
RedisStorage::get(const Digest& key)
{
  const int err = connect();
  if (is_timeout(err)) {
    return nonstd::make_unexpected(Error::timeout);
  } else if (is_error(err)) {
    return nonstd::make_unexpected(Error::error);
  }

  const std::string key_string = get_key_string(key);
  LOG("Redis GET {}", key_string);

  const auto reply = redis_command(m_context, "GET %s", key_string.c_str());
  if (!reply) {
    LOG("Failed to get {} from Redis (NULL)", key_string);
  } else if (reply->type == REDIS_REPLY_STRING) {
    return std::string(reply->str, reply->len);
  } else if (reply->type == REDIS_REPLY_NIL) {
    return nonstd::nullopt;
  } else if (reply->type == REDIS_REPLY_ERROR) {
    LOG("Failed to get {} from Redis: {}", key_string, reply->str);
  } else {
    LOG("Failed to get {} from Redis: unknown reply type {}",
        key_string,
        reply->type);
  }

  return nonstd::make_unexpected(Error::error);
}

nonstd::expected<bool, SecondaryStorage::Error>
RedisStorage::put(const Digest& key,
                  const std::string& value,
                  bool only_if_missing)
{
  const int err = connect();
  if (is_timeout(err)) {
    return nonstd::make_unexpected(Error::timeout);
  } else if (is_error(err)) {
    return nonstd::make_unexpected(Error::error);
  }

  const std::string key_string = get_key_string(key);
  if (only_if_missing) {
    LOG("Redis EXISTS {}", key_string);
    const auto reply =
      redis_command(m_context, "EXISTS %s", key_string.c_str());
    if (!reply) {
      LOG("Failed to check {} in Redis", key_string);
    } else if (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0) {
      LOG("Entry {} already in Redis", key_string);
      return false;
    } else if (reply->type == REDIS_REPLY_ERROR) {
      LOG("Failed to check {} in Redis: {}", key_string, reply->str);
    }
  }

  LOG("Redis SET {}", key_string);
  const auto reply = redis_command(
    m_context, "SET %s %b", key_string.c_str(), value.data(), value.size());
  if (!reply) {
    LOG("Failed to put {} to Redis (NULL)", key_string);
  } else if (reply->type == REDIS_REPLY_STATUS) {
    return true;
  } else if (reply->type == REDIS_REPLY_ERROR) {
    LOG("Failed to put {} to Redis: {}", key_string, reply->str);
  } else {
    LOG("Failed to put {} to Redis: unknown reply type {}",
        key_string,
        reply->type);
  }

  return nonstd::make_unexpected(Error::error);
}

nonstd::expected<bool, SecondaryStorage::Error>
RedisStorage::remove(const Digest& key)
{
  const int err = connect();
  if (is_timeout(err)) {
    return nonstd::make_unexpected(Error::timeout);
  } else if (is_error(err)) {
    return nonstd::make_unexpected(Error::error);
  }

  const std::string key_string = get_key_string(key);
  LOG("Redis DEL {}", key_string);

  const auto reply = redis_command(m_context, "DEL %s", key_string.c_str());
  if (!reply) {
    LOG("Failed to remove {} from Redis (NULL)", key_string);
  } else if (reply->type == REDIS_REPLY_INTEGER) {
    return reply->integer > 0;
  } else if (reply->type == REDIS_REPLY_ERROR) {
    LOG("Failed to remove {} from Redis: {}", key_string, reply->str);
  } else {
    LOG("Failed to remove {} from Redis: unknown reply type {}",
        key_string,
        reply->type);
  }

  return nonstd::make_unexpected(Error::error);
}

std::string
RedisStorage::get_key_string(const Digest& digest) const
{
  return FMT("{}:{}", m_prefix, digest.to_string());
}

} // namespace secondary
} // namespace storage
