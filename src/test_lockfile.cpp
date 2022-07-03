// Copyright (C) 2022 Joel Rosdahl and other contributors
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

#include <Config.hpp>
#include <Lockfile.hpp>
#include <Logging.hpp>
#include <fmtmacros.hpp>
#include <util/string.hpp>

#include <string>
#include <thread>

int
main(int argc, char** argv)
{
  if (argc != 3) {
    PRINT_RAW(stderr, "Usage: test-lockfile PATH SECONDS\n");
    return 1;
  }
  Config config;
  config.update_from_environment();
  Logging::init(config);

  std::string path(argv[1]);
  auto seconds = util::parse_signed(argv[2]);
  if (!seconds) {
    PRINT_RAW(stderr, "Error: Failed to parse seconds\n");
    return 1;
  }

  PRINT_RAW(stdout, "Acquiring\n");
  {
    Lockfile lock(path);
    if (lock.acquired()) {
      PRINT_RAW(stdout, "Acquired\n");
      PRINT(
        stdout, "Sleeping {} second{}\n", *seconds, *seconds == 1 ? "" : "s");
      std::this_thread::sleep_for(std::chrono::seconds{*seconds});
    } else {
      PRINT_RAW(stdout, "Failed to acquire\n");
    }
    PRINT_RAW(stdout, "Releasing\n");
  }
  PRINT_RAW(stdout, "Released\n");
}
