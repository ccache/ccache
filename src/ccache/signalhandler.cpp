// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include "signalhandler.hpp"

#include <ccache/context.hpp>
#include <ccache/util/assertions.hpp>

#include <signal.h> // NOLINT: sigaddset et al are defined in signal.h
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

SignalHandler* g_the_signal_handler = nullptr;
sigset_t g_fatal_signal_set;

const std::vector<int> k_handled_signals = {
  SIGINT,
  SIGTERM,
#ifdef SIGHUP
  SIGHUP,
#endif
#ifdef SIGQUIT
  SIGQUIT,
#endif
};

void
register_signal_handler(int signum)
{
  struct sigaction act = {};
  act.sa_handler = SignalHandler::on_signal;
  act.sa_mask = g_fatal_signal_set;
#ifdef SA_RESTART
  act.sa_flags = SA_RESTART;
#endif
  sigaction(signum, &act, nullptr);
}

void
deregister_signal_handler(int signum)
{
  struct sigaction act = {};
  act.sa_handler = SIG_DFL;
  sigaction(signum, &act, nullptr);
}

} // namespace

SignalHandler::SignalHandler(Context& ctx)
  : m_ctx(ctx)
{
  ASSERT(!g_the_signal_handler);
  g_the_signal_handler = this;

  sigemptyset(&g_fatal_signal_set);
  for (int signum : k_handled_signals) {
    sigaddset(&g_fatal_signal_set, signum);
  }

  for (int signum : k_handled_signals) {
    register_signal_handler(signum);
  }

  signal(SIGPIPE, SIG_IGN); // NOLINT: This is no error, clang-tidy
}

SignalHandler::~SignalHandler()
{
  ASSERT(g_the_signal_handler);

  for (int signum : k_handled_signals) {
    deregister_signal_handler(signum);
  }

  g_the_signal_handler = nullptr;
}

void
SignalHandler::on_signal(int signum)
{
  ASSERT(g_the_signal_handler);
  Context& ctx = g_the_signal_handler->m_ctx;

  // Unregister handler for this signal so that we can send the signal to
  // ourselves at the end of the handler.
  std::ignore = signal(signum, SIG_DFL);

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

void
SignalHandler::block_signals()
{
  sigprocmask(SIG_BLOCK, &g_fatal_signal_set, nullptr);
}

void
SignalHandler::unblock_signals()
{
  sigset_t empty;
  sigemptyset(&empty);
  sigprocmask(SIG_SETMASK, &empty, nullptr);
}

const std::vector<int>&
SignalHandler::get_handled_signals()
{
  return k_handled_signals;
}

SignalHandlerBlocker::SignalHandlerBlocker()
{
  SignalHandler::block_signals();
}

SignalHandlerBlocker::~SignalHandlerBlocker()
{
  SignalHandler::unblock_signals();
}
