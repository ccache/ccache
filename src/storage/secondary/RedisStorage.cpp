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

#include <AtomicFile.hpp>
#include <Digest.hpp>
#include <Logging.hpp>
#include <UmaskScope.hpp>
#include <Util.hpp>
#include <assertions.hpp>
#include <fmtmacros.hpp>
#include <util/file_utils.hpp>
#include <util/string_utils.hpp>

#include <third_party/nonstd/string_view.hpp>

#include <hiredis/hiredis.h>

namespace storage {
namespace secondary {

static struct timeval
milliseconds_to_timeval(const std::string& msec)
{
  int ms = std::stoi(msec);
  struct timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  return tv;
}

static std::string
timeval_to_string(struct timeval tv)
{
  return FMT("{:.3f}s", tv.tv_sec + tv.tv_usec / 1000000.0);
}

static nonstd::optional<struct timeval>
parse_connect_timeout(const AttributeMap& attributes)
{
  const auto it = attributes.find("connect-timeout");
  if (it == attributes.end()) {
    return nonstd::nullopt;
  }
  return milliseconds_to_timeval(it->second);
}

static nonstd::optional<struct timeval>
parse_operation_timeout(const AttributeMap& attributes)
{
  const auto it = attributes.find("operation-timeout");
  if (it == attributes.end()) {
    return nonstd::nullopt;
  }
  return milliseconds_to_timeval(it->second);
}

static nonstd::optional<std::string>
parse_username(const AttributeMap& attributes)
{
  const auto it = attributes.find("username");
  if (it == attributes.end()) {
    return nonstd::nullopt;
  }
  return it->second;
}

static nonstd::optional<std::string>
parse_password(const AttributeMap& attributes)
{
  const auto it = attributes.find("password");
  if (it == attributes.end()) {
    return nonstd::nullopt;
  }
  return it->second;
}

RedisStorage::RedisStorage(const Url& url, const AttributeMap& attributes)
  : m_url(url),
    m_connect_timeout(parse_connect_timeout(attributes)),
    m_operation_timeout(parse_operation_timeout(attributes)),
    m_username(parse_username(attributes)),
    m_password(parse_password(attributes))
{
  m_prefix = "ccache"; // TODO: attribute
  m_context = nullptr;
  m_connected = false;
  m_invalid = false;
}

RedisStorage::~RedisStorage()
{
  disconnect();
  if (m_context) {
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
    LOG("Redis reconnect err: {}", m_context->errstr);
    redisFree(m_context);
    m_context = nullptr;
  }

  ASSERT(m_url.scheme() == "redis");
  std::string host = m_url.host();
  std::string port = m_url.port();
  std::string sock = m_url.path();
  if (m_connect_timeout) {
    LOG("Redis connect timeout {}", timeval_to_string(*m_connect_timeout));
  }
  if (!host.empty()) {
    int p = port.empty() ? 6379 : std::stoi(port);
    if (m_connect_timeout) {
      m_context = redisConnectWithTimeout(host.c_str(), p, *m_connect_timeout);
    } else {
      m_context = redisConnect(host.c_str(), p);
    }
  } else if (!sock.empty()) {
    if (m_connect_timeout) {
      m_context = redisConnectUnixWithTimeout(sock.c_str(), *m_connect_timeout);
    } else {
      m_context = redisConnectUnix(sock.c_str());
    }
  } else {
    LOG("Redis invalid url: {}", m_url.str());
    m_invalid = true;
    return REDIS_ERR;
  }

  if (!m_context) {
    LOG("Redis connect {} err NULL", m_url.str());
    m_invalid = true;
    return REDIS_ERR;
  } else if (m_context->err) {
    LOG("Redis connect {} err: {}", m_url.str(), m_context->errstr);
    m_invalid = true;
    return m_context->err;
  } else {
    if (m_context->connection_type == REDIS_CONN_TCP) {
      LOG(
        "Redis connect tcp {}:{} OK", m_context->tcp.host, m_context->tcp.port);
    }
    if (m_context->connection_type == REDIS_CONN_UNIX) {
      LOG("Redis connect unix {} OK", m_context->unix_sock.path);
    }
    m_connected = true;

    if (m_operation_timeout) {
      LOG("Redis timeout {}", timeval_to_string(*m_operation_timeout));
      if (redisSetTimeout(m_context, *m_operation_timeout) != REDIS_OK) {
        LOG_RAW("Failed to set timeout");
      }
    }

    // TODO: break out AUTH to separate function
    if (m_password) {
      std::string username = m_username ? *m_username : "default";
      LOG("Redis AUTH {} {}", username, "*****"); // don't log m_password !!!
      redisReply* reply;
      if (m_username) {
        reply = static_cast<redisReply*>(redisCommand(
          m_context, "AUTH %s %s", m_username->c_str(), m_password->c_str()));
      } else {
        reply = static_cast<redisReply*>(
          redisCommand(m_context, "AUTH %s", m_password->c_str()));
      }
      if (!reply) {
        LOG("Failed to auth {} in redis", username);
        m_invalid = true;
      } else if (reply->type == REDIS_REPLY_ERROR) {
        LOG("Failed to auth {} in redis: {}", username, reply->str);
        m_invalid = true;
      }
      freeReplyObject(reply);
      if (m_invalid) {
        return REDIS_ERR;
      }
    }

    return REDIS_OK;
  }
}

inline bool
is_error(int err)
{
  return (err != REDIS_OK);
}

inline bool
is_timeout(int err)
{
#ifdef REDIS_ERR_TIMEOUT
  // Only returned for hiredis version 1.0.0 and above
  return (err == REDIS_ERR_TIMEOUT);
#else
  (void)err;
  return false;
#endif
}

void
RedisStorage::disconnect()
{
  if (m_connected) {
    // Note: only the async API actually disconnects from the server
    //       the connection is eventually cleaned up in redisFree()
    LOG_RAW("Redis disconnect");
    m_connected = false;
  }
}

nonstd::expected<nonstd::optional<std::string>, SecondaryStorage::Error>
RedisStorage::get(const Digest& key)
{
  int err = connect();
  if (is_timeout(err)) {
    return nonstd::make_unexpected(Error::timeout);
  } else if (is_error(err)) {
    return nonstd::make_unexpected(Error::error);
  }
  const std::string key_string = get_key_string(key);
  LOG("Redis GET {}", key_string);
  redisReply* reply = static_cast<redisReply*>(
    redisCommand(m_context, "GET %s", key_string.c_str()));
  bool found = false;
  bool missing = false;
  std::string value;
  if (!reply) {
    LOG("Failed to get {} from redis", key_string);
  } else if (reply->type == REDIS_REPLY_ERROR) {
    LOG("Failed to get {} from redis: {}", key_string, reply->str);
  } else if (reply->type == REDIS_REPLY_STRING) {
    value = std::string(reply->str, reply->len);
    found = true;
  } else if (reply->type == REDIS_REPLY_NIL) {
    missing = true;
  }
  freeReplyObject(reply);
  if (found) {
    return value;
  } else if (missing) {
    return nonstd::nullopt;
  } else {
    return nonstd::make_unexpected(Error::error);
  }
}

nonstd::expected<bool, SecondaryStorage::Error>
RedisStorage::put(const Digest& key,
                  const std::string& value,
                  bool only_if_missing)
{
  int err = connect();
  if (is_timeout(err)) {
    return nonstd::make_unexpected(Error::timeout);
  } else if (is_error(err)) {
    return nonstd::make_unexpected(Error::error);
  }
  const std::string key_string = get_key_string(key);
  if (only_if_missing) {
    LOG("Redis EXISTS {}", key_string);
    redisReply* reply = static_cast<redisReply*>(
      redisCommand(m_context, "EXISTS %s", key_string.c_str()));
    int count = 0;
    if (!reply) {
      LOG("Failed to check {} in redis", key_string);
    } else if (reply->type == REDIS_REPLY_ERROR) {
      LOG("Failed to check {} in redis: {}", key_string, reply->str);
    } else if (reply->type == REDIS_REPLY_INTEGER) {
      count = reply->integer;
    }
    freeReplyObject(reply);
    if (count > 0) {
      return false;
    }
  }
  LOG("Redis SET {}", key_string);
  redisReply* reply = static_cast<redisReply*>(redisCommand(
    m_context, "SET %s %b", key_string.c_str(), value.data(), value.size()));
  bool stored = false;
  if (!reply) {
    LOG("Failed to set {} to redis", key_string);
  } else if (reply->type == REDIS_REPLY_ERROR) {
    LOG("Failed to set {} to redis: {}", key_string, reply->str);
  } else if (reply->type == REDIS_REPLY_STATUS) {
    stored = true;
  } else {
    LOG("Failed to set {} to redis: {}", key_string, reply->type);
  }
  freeReplyObject(reply);
  if (stored) {
    return true;
  } else {
    return nonstd::make_unexpected(Error::error);
  }
}

nonstd::expected<bool, SecondaryStorage::Error>
RedisStorage::remove(const Digest& key)
{
  int err = connect();
  if (is_timeout(err)) {
    return nonstd::make_unexpected(Error::timeout);
  } else if (is_error(err)) {
    return nonstd::make_unexpected(Error::error);
  }
  const std::string key_string = get_key_string(key);
  LOG("Redis DEL {}", key_string);
  redisReply* reply = static_cast<redisReply*>(
    redisCommand(m_context, "DEL %s", key_string.c_str()));
  bool removed = false;
  bool missing = false;
  if (!reply) {
    LOG("Failed to del {} in redis", key_string);
  } else if (reply->type == REDIS_REPLY_ERROR) {
    LOG("Failed to del {} in redis: {}", key_string, reply->str);
  } else if (reply->type == REDIS_REPLY_INTEGER) {
    if (reply->integer > 0) {
      removed = true;
    } else {
      missing = true;
    }
  }
  freeReplyObject(reply);
  if (removed) {
    return true;
  } else if (missing) {
    return false;
  } else {
    return nonstd::make_unexpected(Error::error);
  }
}

std::string
RedisStorage::get_key_string(const Digest& digest) const
{
  return FMT("{}:{}", m_prefix, digest.to_string());
}

} // namespace secondary
} // namespace storage
