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
#include <Logging.hpp>
#include <fmtmacros.hpp>
#include <storage/Storage.hpp>
#include <util/string.hpp>

#include <rpc/server.h>

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
  RpcStorageServer(const Config& config) : m_storage(storage::Storage(config))
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

private:
  storage::Storage m_storage;
};

util::Bytes
RpcStorageServer::get(const Digest& key)
{
  LOG("RPC server get {}", key.to_string());
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
  m_storage.put(key, static_cast<core::CacheEntryType>(TYPE_UNKNOWN), value);
  return true;
}

bool
RpcStorageServer::remove(const Digest& key)
{
  LOG("RPC server remove {}", key.to_string());
  m_storage.remove(key, static_cast<core::CacheEntryType>(TYPE_UNKNOWN));
  return true;
}

constexpr const char VERSION_TEXT[] =
  R"({0} version {1}
)";

constexpr const char USAGE_TEXT[] =
  R"(Usage:
    {0} [options]

Options:
    -b, --bind                 address to bind to (default: 127.0.0.1)
    -p, --port                 tcp port to bind to (default: {1})
    -h, --help                 print this help text
    -V, --version              print version and copyright information

See also the manual on <https://ccache.dev/documentation.html>.
)";

const char options_string[] = "b:hp:V";
const option long_options[] = {{"bind", required_argument, nullptr, 'b'},
                               {"help", no_argument, nullptr, 'h'},
                               {"port", required_argument, nullptr, 'p'},
                               {"version", no_argument, nullptr, 'V'},
                               {nullptr, 0, nullptr, 0}};

int
main(int argc, char* const* argv)
{
  Config config;
  config.update_from_environment();

  std::string bind = "127.0.0.1";
  uint32_t port = DEFAULT_PORT;

  int c;
  while ((c = getopt_long(argc, argv, options_string, long_options, nullptr))
         != -1) {
    const std::string arg = optarg ? optarg : std::string();

    switch (c) {
    case 'b': // --bind
      bind = arg;
      break;

    case 'h': // --help
      PRINT(stdout, USAGE_TEXT, Util::base_name(argv[0]), DEFAULT_PORT);
      return EXIT_SUCCESS;

    case 'p': // --port
      port = Util::parse_unsigned(arg);
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

  auto s = RpcStorageServer(config);

  srv.bind("get", [&s](const Digest& key) { return s.get(key); });
  srv.bind("exists", [&s](const Digest& key) { return s.exists(key); });
  srv.bind("put", [&s](const Digest& key, nonstd::span<const uint8_t> value) {
    return s.put(key, value);
  });
  srv.bind("remove", [&s](const Digest& key) { return s.remove(key); });

  srv.run();

  return EXIT_SUCCESS;
}
