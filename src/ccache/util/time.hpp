// Copyright (C) 2023-2025 Joel Rosdahl and other contributors
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

#pragma once

#include <chrono>
#include <ctime>
#include <optional>

namespace util {

// --- Interface ---

using TimePoint =
  std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;

// Thread-safe version of `gmtime(3)`. If `time` is not specified the current
// time of day is used.
std::optional<tm> gmtime(std::optional<TimePoint> time = {});

// Thread-safe version of `localtime(3)`. If `time` is not specified the current
// time of day is used.
std::optional<tm> localtime(std::optional<TimePoint> time = {});

TimePoint now();

template<typename Rep, typename Period>
int32_t nsec_part(std::chrono::duration<Rep, Period> d);

int32_t nsec_part(TimePoint tp);

template<typename Rep, typename Period>
int64_t nsec_tot(std::chrono::duration<Rep, Period> d);

int64_t nsec_tot(TimePoint tp);

template<typename Rep, typename Period>
int64_t sec(std::chrono::duration<Rep, Period> d);

int64_t sec(TimePoint tp);

TimePoint timepoint_from_sec_nsec(int64_t sec, int64_t nsec);

TimePoint timepoint_from_timespec(const timespec& ts);

timespec to_timespec(TimePoint tp);

// --- Inline implementations ---

inline TimePoint
timepoint_from_sec_nsec(int64_t sec, int64_t nsec)
{
  return TimePoint(std::chrono::seconds(sec) + std::chrono::nanoseconds(nsec));
}

inline TimePoint
timepoint_from_timespec(const timespec& ts)
{
  return TimePoint(std::chrono::seconds(ts.tv_sec)
                   + std::chrono::nanoseconds(ts.tv_nsec));
}

inline TimePoint
now()
{
  return std::chrono::system_clock::now();
}

template<typename Rep, typename Period>
inline int64_t
nsec_tot(std::chrono::duration<Rep, Period> d)
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

inline int64_t
nsec_tot(TimePoint tp)
{
  return tp.time_since_epoch().count();
}

template<typename Rep, typename Period>
inline int64_t
sec(std::chrono::duration<Rep, Period> d)
{
  return nsec_tot(d) / 1'000'000'000;
}

inline int64_t
sec(TimePoint tp)
{
  return nsec_tot(tp) / 1'000'000'000;
}

template<typename Rep, typename Period>
inline int32_t
nsec_part(std::chrono::duration<Rep, Period> d)
{
  return nsec_tot(d) % 1'000'000'000;
}

inline int32_t
nsec_part(TimePoint tp)
{
  return nsec_tot(tp) % 1'000'000'000;
}

inline timespec
to_timespec(TimePoint tp)
{
  return {static_cast<time_t>(util::sec(tp)), util::nsec_part(tp)};
}

} // namespace util
