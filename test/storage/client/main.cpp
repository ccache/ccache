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
using storage::remote::Client;

namespace {

const auto k_data_timeout = 10s;     // default for data-timeout property
const auto k_request_timeout = 1min; // default for request-timeout property

constexpr const char USAGE_TEXT[] =
  R"(Usage: {0} IPC_ENDPOINT COMMAND [args...]

This is a CLI tool for testing ccache storage helper implementations.

Commands:
    ping                            check if helper is reachable
    info                            print helper info
    stop                            tell the helper to stop

    exists KEY                      check if a value exists in storage
    get KEY -o FILE                 get a value and output to file
    get KEY -o -                    get a value and output to stdout
    put [-n] KEY -i FILE            put a value from file (-n = no overwrite)
    put [-n] KEY -i -               put a value from stdin (-n = no overwrite)
    put [-n] KEY -v VALUE           put a literal value (-n = no overwrite)
    remove KEY                      remove a value from storage

Notes:
    KEY must be a hexadecimal string (0-9, a-f, A-F).
    IPC_ENDPOINT is a Unix socket path or Windows named pipe name.
)";

void
print_usage(FILE* stream, const char* program_name)
{
  PRINT(stream, USAGE_TEXT, program_name);
}

tl::expected<int, std::string>
cmd_exists(Client& client, const std::vector<std::string>& args)
{
  if (args.size() != 1) {
    return tl::unexpected("exists requires exactly 1 argument: KEY");
  }

  auto key_result = util::parse_base16(args[0]);
  if (!key_result) {
    PRINT(stderr, "Error: Invalid hex key: {}\n", key_result.error());
    return 1;
  }
  const auto& key = *key_result;

  auto result = client.exists(key);

  if (!result) {
    PRINT(stderr, "Error: {}\n", result.error().message);
    return 1;
  }

  if (*result) {
    PRINT(stdout, "yes\n");
    return 0;
  } else {
    PRINT(stderr, "no\n");
    return 2;
  }
}

tl::expected<int, std::string>
cmd_get(Client& client, const std::vector<std::string>& args)
{
  if (args.size() != 3 || args[1] != "-o") {
    return tl::unexpected("missing arguments");
  }

  auto key_result = util::parse_base16(args[0]);
  if (!key_result) {
    return tl::unexpected(FMT("invalid hex key: {}", key_result.error()));
  }
  const auto& key = *key_result;
  const auto& output = args[2];

  auto result = client.get(key);

  if (!result) {
    return tl::unexpected(result.error().message);
  }

  if (!*result) {
    PRINT(stdout, "Key not found: {}", util::format_base16(key));
    return 2;
  }

  const auto& value = **result;

  if (output == "-") {
    std::fwrite(value.data(), 1, value.size(), stdout);
  } else {
    if (auto r = util::write_file(output, value); !r) {
      return tl::unexpected(FMT("failed writing to {}: {}", output, r.error()));
    }
  }

  return 0;
}

tl::expected<int, std::string>
cmd_info(Client& client, const std::vector<std::string>& args)
{
  if (args.size() != 0) {
    return tl::unexpected("info does not take any argument");
  }

  auto result = client.info();

  if (!result) {
    return tl::unexpected(result.error().message);
  }

  PRINT(stdout, "Server identity: {}\n", result->server_identity);
  PRINT(stdout, "Capabilities: {}\n", util::join(client.capabilities(), " "));
  return 0;
}

tl::expected<int, std::string>
cmd_put(Client& client, const std::vector<std::string>& args)
{
  Client::PutFlags flags{.overwrite = true};
  size_t start_idx = 0;

  if (!args.empty() && args[0] == "-n") {
    flags.overwrite = false;
    start_idx = 1;
  }

  if (args.size() - start_idx != 3) {
    return tl::unexpected("missing arguments");
  }

  auto key_result = util::parse_base16(args[start_idx]);
  if (!key_result) {
    return tl::unexpected(FMT("invalid hex key: {}", key_result.error()));
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
        return tl::unexpected(FMT("failed reading from stdin: {}", r.error()));
      }
      value = std::move(*r);
    } else {
      auto r = util::read_file<util::Bytes>(input);
      if (!r) {
        return tl::unexpected(
          FMT("failed reading from {}: {}", input, r.error()));
      }
      value = std::move(*r);
    }
  } else {
    return tl::unexpected(FMT("unknown mode flag: {}", mode));
  }

  auto result = client.put(key, value, flags);

  if (!result) {
    return tl::unexpected(result.error().message);
  }

  if (*result) {
    PRINT(stdout, "Stored key: {}\n", util::format_base16(key));
    return 0;
  } else {
    PRINT(stderr, "Not stored: {}\n", util::format_base16(key));
    return 2;
  }
}

tl::expected<int, std::string>
cmd_remove(Client& client, const std::vector<std::string>& args)
{
  if (args.size() != 1) {
    return tl::unexpected("remove requires exactly 1 argument: KEY");
  }

  auto key_result = util::parse_base16(args[0]);
  if (!key_result) {
    return tl::unexpected(FMT("invalid hex key: {}", key_result.error()));
  }
  const auto& key = *key_result;

  auto result = client.remove(key);

  if (!result) {
    return tl::unexpected(result.error().message);
  }

  if (*result) {
    PRINT(stdout, "Removed key: {}\n", util::format_base16(key));
    return 0;
  } else {
    PRINT(stderr, "Not removed: {}\n", util::format_base16(key));
    return 2;
  }
}

tl::expected<int, std::string>
cmd_stop(Client& client, const std::vector<std::string>& args)
{
  if (!args.empty()) {
    return tl::unexpected("stop takes no arguments");
  }

  auto result = client.stop();

  if (!result) {
    return tl::unexpected(result.error().message);
  }

  PRINT(stdout, "Helper stopped\n");
  return 0;
}

tl::expected<int, std::string>
cmd_ping(const std::vector<std::string>& args)
{
  if (!args.empty()) {
    return tl::unexpected("ping takes no arguments");
  }

  // Connection and protocol verification already done in main.
  PRINT(stdout, "Helper is reachable\n");
  return 0;
}

} // namespace

tl::expected<void, std::string>
require_capability(const Client& client, Client::Capability capability)
{
  if (!client.has_capability(capability)) {
    return tl::unexpected(
      FMT("storage helper does not support capability \"{}\"",
          to_string(capability)));
  }
  return {};
}

tl::expected<int, std::string>
handle_command(Client& client,
               const std::string& command,
               const std::vector<std::string>& args)
{
  int result = 0;

  if (command == "ping") {
    TRY_ASSIGN(result, cmd_ping(args));
  } else if (command == "exists") {
    TRY(require_capability(client, Client::Capability::exists));
    TRY_ASSIGN(result, cmd_exists(client, args));
  } else if (command == "get") {
    TRY(require_capability(client, Client::Capability::get_put_remove));
    TRY_ASSIGN(result, cmd_get(client, args));
  } else if (command == "info") {
    TRY(require_capability(client, Client::Capability::info));
    TRY_ASSIGN(result, cmd_info(client, args));
  } else if (command == "put") {
    TRY(require_capability(client, Client::Capability::get_put_remove));
    TRY_ASSIGN(result, cmd_put(client, args));
  } else if (command == "remove") {
    TRY(require_capability(client, Client::Capability::get_put_remove));
    TRY_ASSIGN(result, cmd_remove(client, args));
  } else if (command == "stop") {
    TRY_ASSIGN(result, cmd_stop(client, args));
  } else {
    return tl::unexpected(FMT("Unknown command: {}", command));
  }

  return result;
}

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

  Client client(k_data_timeout, k_request_timeout);
  auto connect_result = client.connect(ipc_endpoint);

  if (!connect_result) {
    PRINT(stderr,
          "Failed connecting to {}: {}\n",
          ipc_endpoint,
          connect_result.error().message);
    return 1;
  }

  const std::string command = argv[2];
  std::vector<std::string> args;
  for (int i = 3; i < argc; ++i) {
    args.push_back(argv[i]);
  }

  auto result = handle_command(client, command, args);
  if (result) {
    return *result;
  } else {
    PRINT(stderr, "Error: {}\n", result.error());
    return 1;
  }

  return 0;
}
