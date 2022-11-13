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

#include "RpcStorage.hpp"
#include "RpcStorage_msgpack.hpp"

#include <Config.hpp>
#include <Hash.hpp>
#include <Logging.hpp>
#include <fmtmacros.hpp>
#include <storage/Storage.hpp>
#include <util/file.hpp>
#include <util/string.hpp>

#include <rpc/server.h>
#include <rpc/this_handler.h>
#include <rpc/this_session.h>

#include <iostream>
#include <thread>

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#elif defined(_WIN32)
#  include <third_party/win32/getopt.h>
#else
extern "C" {
#  include <third_party/getopt_long.h>
}
#endif

const uint32_t DEFAULT_PORT = 8080;

extern const char CCACHE_VERSION[];

// the Type only matters for local
const uint8_t TYPE_UNKNOWN = 0xff;

class RpcStorageServer
{
public:
  RpcStorageServer(const Config& config, bool auth, const std::string& pass)
    : m_storage(storage::Storage(config)),
      m_requirepass(auth),
      m_password(hash(pass))
  {
    m_storage.initialize();
    LOG("RPC storage: {}", m_storage.get_remote_storage_config_for_logging());
  }

  ~RpcStorageServer()
  {
    m_storage.finalize();
  }

  util::Bytes get(const Digest& key);

  bool
  exists(const Digest& key)
  {
    return get(key).size() > 0;
  }

  bool put(const Digest& key, nonstd::span<const uint8_t> value);

  bool remove(const Digest& key);

  bool auth(const std::string& pass);

private:
  storage::Storage m_storage;
  bool m_requirepass;
  std::unordered_map<rpc::session_id_t, std::string> m_pass;
  std::string m_password;

  bool authorized();
  std::string hash(const std::string& pass);
  bool match(const std::string& pass);
};

util::Bytes
RpcStorageServer::get(const Digest& key)
{
  LOG("RPC server get {}", key.to_string());
  if (!authorized()) {
    rpc::this_handler().respond_error("auth required");
  }
  util::Bytes cache_entry_data;
  m_storage.get(key,
                static_cast<core::CacheEntryType>(TYPE_UNKNOWN),
                [&](util::Bytes&& value) {
                  cache_entry_data = std::move(value);
                  return true;
                });
  return cache_entry_data;
}

bool
RpcStorageServer::put(const Digest& key, nonstd::span<const uint8_t> value)
{
  LOG("RPC server put {} [{} bytes]", key.to_string(), value.size());
  if (!authorized()) {
    rpc::this_handler().respond_error("auth required");
  }
  m_storage.put(key, static_cast<core::CacheEntryType>(TYPE_UNKNOWN), value);
  return true;
}

bool
RpcStorageServer::remove(const Digest& key)
{
  LOG("RPC server remove {}", key.to_string());
  if (!authorized()) {
    rpc::this_handler().respond_error("auth required");
  }
  m_storage.remove(key, static_cast<core::CacheEntryType>(TYPE_UNKNOWN));
  return true;
}

bool
RpcStorageServer::auth(const std::string& pass)
{
  auto id = rpc::this_session().id();
  LOG("RPC server auth (id {})", id);
  m_pass[id] = hash(pass);
  return authorized();
}

bool
RpcStorageServer::authorized()
{
  if (!m_requirepass) {
    return true;
  }
  auto id = rpc::this_session().id();
  return match(m_pass[id]);
}

std::string
RpcStorageServer::hash(const std::string& pass)
{
  return Hash().hash(pass).digest().to_string();
}

bool
RpcStorageServer::match(const std::string& pass)
{
  return pass == m_password;
}

constexpr const char VERSION_TEXT[] =
  R"({0} version {1}
)";

constexpr const char USAGE_TEXT[] =
  R"(Usage:
    {0} [options]

Options:
    -a, --auth                 require authentication (default: false)
    -b, --bind                 address to bind to (default: 127.0.0.1)
    -P, --passwd               path to the password file (for auth)
    -p, --port                 tcp port to bind to (default: {1})
    -n, --threads              number of worker threads (default: 1)
    -h, --help                 print this help text
    -V, --version              print version and copyright information

See also the manual on <https://ccache.dev/documentation.html>.
)";

const char options_string[] = "ab:hP:p:n:V";
const option long_options[] = {{"auth", no_argument, nullptr, 'a'},
                               {"bind", required_argument, nullptr, 'b'},
                               {"help", no_argument, nullptr, 'h'},
                               {"passwd", required_argument, nullptr, 'P'},
                               {"port", required_argument, nullptr, 'p'},
                               {"threads", required_argument, nullptr, 'n'},
                               {"version", no_argument, nullptr, 'V'},
                               {nullptr, 0, nullptr, 0}};

std::string
read_file(const std::string& path)
{
  const auto data = util::read_file<std::string>(path, 0);
  if (!data) {
    throw std::runtime_error("read_file");
  }
  return *data;
}

int
main(int argc, char* const* argv)
{
  Config config;
  config.update_from_environment();

  bool auth = false;
  std::string bind = "127.0.0.1";
  std::string pass;
  uint32_t port = DEFAULT_PORT;
  uint32_t threads = 1;

  int c;
  while ((c = getopt_long(argc, argv, options_string, long_options, nullptr))
         != -1) {
    const std::string arg = optarg ? optarg : std::string();

    switch (c) {
    case 'a': // --auth
      auth = true;
      break;

    case 'b': // --bind
      bind = arg;
      break;

    case 'h': // --help
      PRINT(stdout, USAGE_TEXT, Util::base_name(argv[0]), DEFAULT_PORT);
      return EXIT_SUCCESS;

    case 'P': // --passwd
      pass = read_file(arg);
      break;

    case 'p': // --port
      port = Util::parse_unsigned(arg);
      break;

    case 'n': // --threads
      threads = Util::parse_unsigned(arg);
      break;

    case 'V': // --version
      PRINT(stdout, VERSION_TEXT, Util::base_name(argv[0]), CCACHE_VERSION);
      PRINT(stdout, "Features: {}\n", storage::get_features_excluding("rpc"));
      return EXIT_SUCCESS;

    case '?': // unknown option
      return EXIT_FAILURE;
    }
  }

  // export CCACHE_REMOTE_ONLY=true
  ASSERT(config.remote_only());
  // export CCACHE_REMOTE_STORAGE
  ASSERT(config.remote_storage() != "");
  ASSERT(!util::starts_with(config.remote_storage(), "rpc"));
  // export CCACHE_LOGFILE=server.log
  Logging::init(config);

  rpc::server srv(bind, port);
  LOG("RPC listening to {}:{}", bind, port);
  LOG("RPC authentication required: {}", auth);

  auto s = RpcStorageServer(config, auth, pass);

  srv.bind("get", [&s](const Digest& key) { return s.get(key); });
  srv.bind("exists", [&s](const Digest& key) { return s.exists(key); });
  srv.bind("put", [&s](const Digest& key, nonstd::span<const uint8_t> value) {
    return s.put(key, value);
  });
  srv.bind("remove", [&s](const Digest& key) { return s.remove(key); });
  srv.bind("auth", [&s](const std::string& pass) { return s.auth(pass); });

  if (threads == 1) {
    srv.run();
  } else {
    if (threads == 0) {
      threads = std::thread::hardware_concurrency();
    }
    LOG("RPC using {} worker threads", threads);
    srv.async_run(threads);
    std::cin.ignore();
  }

  return EXIT_SUCCESS;
}
