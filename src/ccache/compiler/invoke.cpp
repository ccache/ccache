// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#include <ccache/args.hpp>
#include <ccache/compiler/console.hpp>
#include <ccache/compiler/invoke.hpp>
#include <ccache/context.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/execute.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/temporaryfile.hpp>
#include <ccache/util/umaskscope.hpp>

#include <fcntl.h>

namespace ccache::compiler {

namespace {
struct GetTmpFdResult
{
  util::Fd fd;
  fs::path path;
};

GetTmpFdResult
get_tmp_fd(Context& ctx,
           const std::string_view description,
           const bool capture_output)
{
  if (capture_output) {
    auto tmp_stdout =
      util::value_or_throw<core::Fatal>(util::TemporaryFile::create(
        FMT("{}/{}", ctx.config.temporary_dir(), description)));
    ctx.register_pending_tmp_file(tmp_stdout.path);
    return {std::move(tmp_stdout.fd), std::move(tmp_stdout.path)};
  } else {
    const auto dev_null_path = util::get_dev_null_path();
    return {util::Fd(open(dev_null_path, O_WRONLY | O_BINARY)), dev_null_path};
  }
}
} // namespace

Invocation::Invocation(Context& ctx, Args& args, bool capture_stdout) noexcept
  : m_ctx(ctx),
    m_args(args),
    m_capture_stdout(capture_stdout)
{
}

auto
Invocation::execute() const -> tl::expected<Execution, core::Statistic>
{
  util::UmaskScope umask_scope(m_ctx.original_umask);

  if (m_ctx.diagnostics_color_failed) {
    DEBUG_ASSERT(m_ctx.config.compiler() == Compiler::type::gcc);
    m_args.erase_last("-fdiagnostics-color");
  }

  auto tmp_stdout = get_tmp_fd(m_ctx, "stdout", m_capture_stdout);
  auto tmp_stderr = get_tmp_fd(m_ctx, "stderr", true);

  Execution result;

  result.m_exit_status = ::execute(m_ctx,
                                   m_args.to_argv().data(),
                                   std::move(tmp_stdout.fd),
                                   std::move(tmp_stderr.fd));
  if (result.m_exit_status != 0 && !m_ctx.diagnostics_color_failed
      && m_ctx.config.compiler() == Compiler::type::gcc) {
    const auto errors = util::read_file<std::string>(tmp_stderr.path);
    if (errors && errors->find("fdiagnostics-color") != std::string::npos) {
      // GCC versions older than 4.9 don't understand -fdiagnostics-color, and
      // non-GCC compilers misclassified as Compiler::type::gcc might not do it
      // either. We assume that if the error message contains
      // "fdiagnostics-color" then the compilation failed due to
      // -fdiagnostics-color being unsupported and we then retry without the
      // flag. (Note that there intentionally is no leading dash in
      // "fdiagnostics-color" since some compilers don't include the dash in the
      // error message.)
      LOG_RAW("-fdiagnostics-color is unsupported; trying again without it");

      m_ctx.diagnostics_color_failed = true;
      return Invocation::execute();
    }
  }

  if (m_capture_stdout) {
    auto stdout_data_result = util::read_file<util::Bytes>(tmp_stdout.path);
    if (!stdout_data_result) {
      LOG("Failed to read {} (cleanup in progress?): {}",
          tmp_stdout.path,
          stdout_data_result.error());
      return tl::unexpected(core::Statistic::missing_cache_file);
    }
    result.m_stdout_data = std::move(*stdout_data_result);
  }

  auto stderr_data_result = util::read_file<util::Bytes>(tmp_stderr.path);
  if (!stderr_data_result) {
    LOG("Failed to read {} (cleanup in progress?): {}",
        tmp_stderr.path,
        stderr_data_result.error());
    return tl::unexpected(core::Statistic::missing_cache_file);
  }
  result.m_stderr_data = std::move(*stderr_data_result);

  // Merge stderr from the preprocessor (if any) and stderr from the real
  // compiler.
  if (!m_ctx.cpp_stderr_data.empty()) {
    result.m_stderr_data.insert(result.m_stderr_data.begin(),
                                m_ctx.cpp_stderr_data.begin(),
                                m_ctx.cpp_stderr_data.end());
  }

  result.m_console =
    Console::process(m_ctx,
                     util::to_string_view(result.m_stdout_data),
                     util::to_string_view(result.m_stderr_data));

  return result;
}

auto
Execution::exit_status() const noexcept -> int
{
  return m_exit_status;
}

auto
Execution::stdout_data() const noexcept -> const util::Bytes&
{
  return m_stdout_data;
}

auto
Execution::stderr_data() const noexcept -> const util::Bytes&
{
  return m_stderr_data;
}

auto
Execution::stderr_data() noexcept -> util::Bytes&
{
  return m_stderr_data;
}

auto
Execution::console() const noexcept -> const Console&
{
  return m_console;
}
} // namespace ccache::compiler
