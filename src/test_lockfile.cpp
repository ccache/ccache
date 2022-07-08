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
#include <Logging.hpp>
#include <fmtmacros.hpp>
#include <util/LockFile.hpp>
#include <util/string.hpp>

#include <memory>
#include <string>
#include <thread>

int
main(int argc, char** argv)
{
  if (argc != 5) {
    PRINT_RAW(stderr,
              "Usage: test-lockfile PATH SECONDS <short|long>"
              " <blocking|non-blocking>\n");
    return 1;
  }
  Config config;
  config.update_from_environment();
  Logging::init(config);

  const std::string path(argv[1]);
  const auto seconds = util::parse_signed(argv[2]);
  const bool long_lived = std::string(argv[3]) == "long";
  const bool blocking = std::string(argv[4]) == "blocking";
  if (!seconds) {
    PRINT_RAW(stderr, "Error: Failed to parse seconds\n");
    return 1;
  }

  using LockFilePtr = std::unique_ptr<util::LockFile>;
  LockFilePtr lock_file;
  lock_file = long_lived
                ? LockFilePtr{std::make_unique<util::LongLivedLockFile>(path)}
                : LockFilePtr{std::make_unique<util::ShortLivedLockFile>(path)};
  const auto mode = blocking ? util::LockFileGuard::Mode::blocking
                             : util::LockFileGuard::Mode::non_blocking;

  PRINT(stdout, "{}\n", blocking ? "Acquiring" : "Trying to acquire");
  bool acquired = false;
  {
    util::LockFileGuard lock(*lock_file, mode);
    acquired = lock.acquired();
    if (acquired) {
      PRINT_RAW(stdout, "Acquired\n");
      PRINT(
        stdout, "Sleeping {} second{}\n", *seconds, *seconds == 1 ? "" : "s");
      std::this_thread::sleep_for(std::chrono::seconds{*seconds});
    } else {
      PRINT(stdout, "{} acquire\n", blocking ? "Failed to" : "Did not");
    }
    if (acquired) {
      PRINT_RAW(stdout, "Releasing\n");
    }
  }
  if (acquired) {
    PRINT_RAW(stdout, "Released\n");
  }
}
