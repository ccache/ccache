// Copyright (C) 2020-2026 Joel Rosdahl and other contributors
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

#ifndef _WIN32
#  include <signal.h>
#endif

class Context;

class SignalHandler
{
public:
  SignalHandler(Context& ctx);
  ~SignalHandler();

  static void on_signal(int signum);
  static bool is_active();
#ifndef _WIN32
  static const sigset_t& get_signals_to_reset();
#endif

private:
#ifndef _WIN32
  Context& m_ctx;
#endif
};

class SignalHandlerBlocker
{
public:
  SignalHandlerBlocker();
  ~SignalHandlerBlocker();

private:
#ifndef _WIN32
  sigset_t m_original_mask;
  bool m_active;
#endif
};

#ifdef _WIN32
inline SignalHandler::SignalHandler(Context&)
{
}

inline SignalHandler::~SignalHandler()
{
}

inline SignalHandlerBlocker::SignalHandlerBlocker()
{
}

inline SignalHandlerBlocker::~SignalHandlerBlocker()
{
}
#endif
