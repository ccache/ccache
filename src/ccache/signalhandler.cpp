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

#include "signalhandler.hpp"

#include <ccache/context.hpp>
#include <ccache/util/assertions.hpp>

#include <signal.h> // NOLINT: sigaddset et al are defined in signal.h
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

namespace {

SignalHandler* g_the_signal_handler = nullptr;

// Cleanup signals caught by ccache. These are blocked while handling a signal
// or starting a compiler.
sigset_t g_fatal_signal_set;

// Signals that were not originally ignored. There are reset default in spawned
// compilers so they don't inherit ccache's signal changes (including SIGPIPE).
sigset_t g_to_reset_signals_set;

struct OriginalSignalAction
{
  int signum;
  struct sigaction action;
};

std::vector<OriginalSignalAction> g_original_actions;
const auto k_sig_ign = SIG_IGN; // NOLINT: This is no error, clang-tidy

const std::vector<int> k_handled_signals = {
  SIGINT,
  SIGTERM,
#ifdef SIGHUP
  SIGHUP,
#endif
#ifdef SIGQUIT
  SIGQUIT,
#endif
  SIGPIPE,
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

} // namespace

SignalHandler::SignalHandler(Context& ctx)
  : m_ctx(ctx)
{
  ASSERT(!g_the_signal_handler);
  g_the_signal_handler = this;

  g_original_actions.clear();
  g_original_actions.reserve(k_handled_signals.size());
  sigemptyset(&g_fatal_signal_set);
  sigemptyset(&g_to_reset_signals_set);
  for (int signum : k_handled_signals) {
    struct sigaction oldact = {};
    if (signum == SIGPIPE) {
      // Ignore SIGPIPE so send(2) on a closed connection returns EPIPE instead
      // of terminating ccache.
      struct sigaction ignore_act = {};
      ignore_act.sa_handler = k_sig_ign;
      sigaction(signum, &ignore_act, &oldact);
    } else {
      sigaction(signum, nullptr, &oldact);
    }
    g_original_actions.push_back({signum, oldact});
    if (oldact.sa_handler != k_sig_ign) {
      sigaddset(&g_to_reset_signals_set, signum);
    }
    if (signum != SIGPIPE && oldact.sa_handler != k_sig_ign) {
      sigaddset(&g_fatal_signal_set, signum);
    }
  }

  for (size_t i = 0; i < k_handled_signals.size(); ++i) {
    if (k_handled_signals[i] != SIGPIPE
        && g_original_actions[i].action.sa_handler != k_sig_ign) {
      register_signal_handler(k_handled_signals[i]);
    }
  }
}

SignalHandler::~SignalHandler()
{
  ASSERT(g_the_signal_handler);

  for (const auto& action : g_original_actions) {
    sigaction(action.signum, &action.action, nullptr);
  }

  g_the_signal_handler = nullptr;
}

bool
SignalHandler::is_active()
{
  return bool(g_the_signal_handler);
}

const sigset_t&
SignalHandler::get_signals_to_reset()
{
  return g_to_reset_signals_set;
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

SignalHandlerBlocker::SignalHandlerBlocker()
  : m_active(SignalHandler::is_active())
{
  if (m_active) {
    sigprocmask(SIG_BLOCK, &g_fatal_signal_set, &m_original_mask);
  }
}

SignalHandlerBlocker::~SignalHandlerBlocker()
{
  if (m_active) {
    sigprocmask(SIG_SETMASK, &m_original_mask, nullptr);
  }
}
