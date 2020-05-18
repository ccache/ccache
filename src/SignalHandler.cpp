// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "SignalHandler.hpp"

#ifndef _WIN32

#  include "Context.hpp"

namespace {

SignalHandler* g_the_signal_handler = nullptr;
sigset_t g_fatal_signal_set;

void
register_signal_handler(int signum)
{
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = SignalHandler::on_signal;
  act.sa_mask = g_fatal_signal_set;
#  ifdef SA_RESTART
  act.sa_flags = SA_RESTART;
#  endif
  sigaction(signum, &act, nullptr);
}

} // namespace

SignalHandler::SignalHandler(Context& ctx) : m_ctx(ctx)
{
  assert(!g_the_signal_handler);
  g_the_signal_handler = this;

  sigemptyset(&g_fatal_signal_set);
  sigaddset(&g_fatal_signal_set, SIGINT);
  sigaddset(&g_fatal_signal_set, SIGTERM);
#  ifdef SIGHUP
  sigaddset(&g_fatal_signal_set, SIGHUP);
#  endif
#  ifdef SIGQUIT
  sigaddset(&g_fatal_signal_set, SIGQUIT);
#  endif

  register_signal_handler(SIGINT);
  register_signal_handler(SIGTERM);
#  ifdef SIGHUP
  register_signal_handler(SIGHUP);
#  endif
#  ifdef SIGQUIT
  register_signal_handler(SIGQUIT);
#  endif
}

SignalHandler::~SignalHandler()
{
  assert(g_the_signal_handler);
  g_the_signal_handler = nullptr;
}

void
SignalHandler::on_signal(int signum)
{
  assert(g_the_signal_handler);
  Context& ctx = g_the_signal_handler->m_ctx;

  // Unregister handler for this signal so that we can send the signal to
  // ourselves at the end of the handler.
  signal(signum, SIG_DFL);

  // If ccache was killed explicitly, then bring the compiler subprocess (if
  // any) with us as well.
  if (signum == SIGTERM && ctx.compiler_pid != 0
      && waitpid(ctx.compiler_pid, nullptr, WNOHANG) == 0) {
    kill(ctx.compiler_pid, signum);
  }

  ctx.unlink_pending_tmp_files_signal_safe();

  if (ctx.compiler_pid != 0) {
    // Wait for compiler subprocess to exit before we snuff it.
    waitpid(ctx.compiler_pid, nullptr, 0);
  }

  // Resend signal to ourselves to exit properly after returning from the
  // handler.
  kill(getpid(), signum);
}

#else // !_WIN32

SignalHandler::SignalHandler(Context& ctx) : m_ctx(ctx)
{
}

SignalHandler::~SignalHandler()
{
}

#endif // !_WIN32

void
SignalHandler::block_signals()
{
#ifndef _WIN32
  sigprocmask(SIG_BLOCK, &g_fatal_signal_set, nullptr);
#endif
}

void
SignalHandler::unblock_signals()
{
#ifndef _WIN32
  sigset_t empty;
  sigemptyset(&empty);
  sigprocmask(SIG_SETMASK, &empty, nullptr);
#endif
}

SignalHandlerBlocker::SignalHandlerBlocker()
{
  SignalHandler::block_signals();
}

SignalHandlerBlocker::~SignalHandlerBlocker()
{
  SignalHandler::unblock_signals();
}
