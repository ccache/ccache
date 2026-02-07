// Copyright (C) 2025-2026 Joel Rosdahl and other contributors
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

#include <ccache/storage/remote/client.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/conversion.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/string.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

using namespace std::chrono_literals;

namespace {

const auto k_data_timeout = 1000ms;    // default for data-timeout property
const auto k_request_timeout = 5000ms; // default for request-timeout property

constexpr const char USAGE_TEXT[] =
  R"(Usage: {0} IPC_ENDPOINT COMMAND [args...]

This is a CLI tool for testing ccache storage helper implementations.

Commands:
    ping                            check if helper is reachable
    get KEY -o FILE                 get a value and output to file
    get KEY -o -                    get a value and output to stdout
    put [--overwrite] KEY -i FILE   put a value from file
    put [--overwrite] KEY -i -      put a value from stdin
    put [--overwrite] KEY -v VALUE  put a literal value
    remove KEY                      remove a value from storage
    stop                            tell the helper to stop

Notes:
    KEY must be a hexadecimal string (0-9, a-f, A-F).
    IPC_ENDPOINT is a Unix socket path or Windows named pipe name.
)";

void
print_usage(FILE* stream, const char* program_name)
{
  PRINT(stream, USAGE_TEXT, program_name);
}

int
cmd_get(storage::remote::Client& client, const std::vector<std::string>& args)
{
  if (args.size() != 3 || args[1] != "-o") {
    PRINT(stderr, "Error: get requires: KEY -o OUTPUT\n");
    PRINT(stderr, "  where OUTPUT is a file path or - for stdout\n");
    return 1;
  }

  auto key_result = util::parse_base16(args[0]);
  if (!key_result) {
    PRINT(stderr, "Error: Invalid hex key: {}\n", key_result.error());
    return 1;
  }
  const auto& key = *key_result;
  const auto& output = args[2];

  auto result = client.get(key);

  if (!result) {
    PRINT(stderr, "Error: {}\n", result.error().message);
    return 1;
  }

  if (!*result) {
    PRINT(stderr, "Key not found: {}\n", util::format_base16(key));
    return 2;
  }

  const auto& value = **result;

  if (output == "-") {
    std::fwrite(value.data(), 1, value.size(), stdout);
  } else {
    if (auto r = util::write_file(output, value); !r) {
      PRINT(stderr, "Error writing to {}: {}", output, r.error());
      return 1;
    }
  }

  return 0;
}

int
cmd_put(storage::remote::Client& client, const std::vector<std::string>& args)
{
  storage::remote::Client::PutFlags flags;
  size_t start_idx = 0;

  if (!args.empty() && args[0] == "--overwrite") {
    flags.overwrite = true;
    start_idx = 1;
  }

  if (args.size() - start_idx != 3) {
    PRINT(stderr,
          "Error: put requires: [--overwrite] KEY -i INPUT\n"
          "                 or: [--overwrite] KEY -v VALUE\n"
          "  where INPUT is a file path or - for stdin\n");
    return 1;
  }

  auto key_result = util::parse_base16(args[start_idx]);
  if (!key_result) {
    PRINT(stderr, "Error: Invalid hex key: {}\n", key_result.error());
    return 1;
  }
  const auto& key = *key_result;
  const auto& mode = args[start_idx + 1];
  const auto& input = args[start_idx + 2];

  util::Bytes value;

  if (mode == "-v") {
    value = input;
  } else if (mode == "-i") {
    if (input == "-") {
      auto r = util::read_fd(STDIN_FILENO);
      if (!r) {
        PRINT(stderr, "Error reading from stdin: {}", r.error());
        return 1;
      }
      value = std::move(*r);
    } else {
      auto r = util::read_file<util::Bytes>(input);
      if (!r) {
        PRINT(stderr, "Error reading from {}: {}", input, r.error());
        return 1;
      }
      value = std::move(*r);
    }
  } else {
    PRINT(stderr, "Error: Unknown mode \"{}\". Use -v or -i\n", mode);
    return 1;
  }

  auto result = client.put(key, value, flags);

  if (!result) {
    PRINT(stderr, "Error: {}\n", result.error().message);
    return 1;
  }

  if (*result) {
    PRINT(stdout, "Stored key: {}\n", util::format_base16(key));
    return 0;
  } else {
    PRINT(stderr, "Not stored: {}\n", util::format_base16(key));
    return 2;
  }
}

int
cmd_remove(storage::remote::Client& client,
           const std::vector<std::string>& args)
{
  if (args.size() != 1) {
    PRINT(stderr, "Error: remove requires exactly 1 argument: KEY\n");
    return 1;
  }

  auto key_result = util::parse_base16(args[0]);
  if (!key_result) {
    PRINT(stderr, "Error: Invalid hex key: {}\n", key_result.error());
    return 1;
  }
  const auto& key = *key_result;

  auto result = client.remove(key);

  if (!result) {
    PRINT(stderr, "Error: {}\n", result.error().message);
    return 1;
  }

  if (*result) {
    PRINT(stdout, "Removed key: {}\n", util::format_base16(key));
    return 0;
  } else {
    PRINT(stderr, "Not removed: {}\n", util::format_base16(key));
    return 2;
  }
}

int
cmd_stop(storage::remote::Client& client, const std::vector<std::string>& args)
{
  if (!args.empty()) {
    PRINT(stderr, "Error: stop takes no arguments\n");
    return 1;
  }

  auto result = client.stop();

  if (!result) {
    PRINT(stderr, "Error: {}\n", result.error().message);
    return 1;
  }

  PRINT(stdout, "Helper stopped\n");
  return 0;
}

int
cmd_ping(storage::remote::Client& client, const std::vector<std::string>& args)
{
  if (!args.empty()) {
    PRINT(stderr, "Error: ping takes no arguments\n");
    return 1;
  }

  // Connection and protocol verification already done in main.
  PRINT(stdout, "Helper is reachable\n");
  return 0;
}

} // namespace

int
main(int argc, char* argv[])
{
  if (argc >= 2
      && (std::string_view(argv[1]) == "-h"
          || std::string_view(argv[1]) == "--help")) {
    print_usage(stdout, argv[0]);
    return 0;
  }
  if (argc < 3) {
    print_usage(stderr, argv[0]);
    return 1;
  }

  const std::string ipc_endpoint =
#ifdef _WIN32
    FMT("\\\\.\\pipe\\{}", argv[1]);
#else
    argv[1];
#endif
  const std::string command = argv[2];

  std::vector<std::string> cmd_args;
  for (int i = 3; i < argc; ++i) {
    cmd_args.push_back(argv[i]);
  }

  storage::remote::Client client(k_data_timeout, k_request_timeout);
  auto connect_result = client.connect(ipc_endpoint);

  if (!connect_result) {
    PRINT(stderr,
          "Failed to connect to {}: {}\n",
          ipc_endpoint,
          connect_result.error().message);
    return 1;
  }

  if (client.protocol_version()
      != storage::remote::Client::k_protocol_version) {
    PRINT(
      stderr, "Unsupported protocol version: {}\n", client.protocol_version());
    return 1;
  }

  if (!client.has_capability(
        storage::remote::Client::Capability::get_put_remove_stop)) {
    PRINT(stderr, "Helper does not support get/put/remove/stop operations\n");
    return 1;
  }

  if (command == "ping") {
    return cmd_ping(client, cmd_args);
  } else if (command == "get") {
    return cmd_get(client, cmd_args);
  } else if (command == "put") {
    return cmd_put(client, cmd_args);
  } else if (command == "remove") {
    return cmd_remove(client, cmd_args);
  } else if (command == "stop") {
    return cmd_stop(client, cmd_args);
  } else {
    PRINT(stderr, "Unknown command: {}\n\n", command);
    print_usage(stderr, argv[0]);
    return 1;
  }
}
