// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

#include "ResultRetriever.hpp"

#include "Context.hpp"
#include "Depfile.hpp"
#include "Logging.hpp"

#include <Context.hpp>
#include <Stat.hpp>
#include <core/MsvcShowIncludesOutput.hpp>
#include <core/exceptions.hpp>
#include <core/wincompat.hpp>
#include <fmtmacros.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/string.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace core {

using Result::FileType;

ResultRetriever::ResultRetriever(const Context& ctx,
                                 std::optional<Digest> result_key)
  : m_ctx(ctx),
    m_result_key(result_key)
{
}

void
ResultRetriever::on_embedded_file(uint8_t file_number,
                                  FileType file_type,
                                  nonstd::span<const uint8_t> data)
{
  LOG("Reading embedded entry #{} {} ({} bytes)",
      file_number,
      Result::file_type_to_string(file_type),
      data.size());

  if (file_type == FileType::stdout_output) {
    Util::send_to_fd(
      m_ctx,
      util::to_string_view(MsvcShowIncludesOutput::strip_includes(m_ctx, data)),
      STDOUT_FILENO);
  } else if (file_type == FileType::stderr_output) {
    Util::send_to_fd(m_ctx, util::to_string_view(data), STDERR_FILENO);
  } else {
    const auto dest_path = get_dest_path(file_type);
    if (dest_path.empty()) {
      LOG_RAW("Not writing");
    } else if (dest_path == "/dev/null") {
      LOG_RAW("Not writing to /dev/null");
    } else {
      LOG("Writing to {}", dest_path);
      if (file_type == FileType::dependency) {
        write_dependency_file(dest_path, data);
      } else {
        util::throw_on_error<WriteError>(
          util::write_file(dest_path, data),
          FMT("Failed to write to {}: ", dest_path));
      }
    }
  }
}

void
ResultRetriever::on_raw_file(uint8_t file_number,
                             FileType file_type,
                             uint64_t file_size)
{
  LOG("Reading raw entry #{} {} ({} bytes)",
      file_number,
      Result::file_type_to_string(file_type),
      file_size);

  if (!m_result_key) {
    throw core::Error("Raw entry for non-local result");
  }
  const auto raw_file_path =
    m_ctx.storage.local.get_raw_file_path(*m_result_key, file_number);
  const auto st = Stat::stat(raw_file_path, Stat::OnError::throw_error);
  if (st.size() != file_size) {
    throw core::Error(
      FMT("Bad file size of {} (actual {} bytes, expected {} bytes)",
          raw_file_path,
          st.size(),
          file_size));
  }

  const auto dest_path = get_dest_path(file_type);
  if (!dest_path.empty()) {
    try {
      Util::clone_hard_link_or_copy_file(
        m_ctx.config, raw_file_path, dest_path, false);
    } catch (core::Error& e) {
      throw WriteError(FMT("Failed to clone/link/copy {} to {}: {}",
                           raw_file_path,
                           dest_path,
                           e.what()));
    }

    // Update modification timestamp to save the file from LRU cleanup (and, if
    // hard-linked, to make the object file newer than the source file).
    util::set_timestamps(raw_file_path);
  } else {
    // Should never happen.
    LOG("Did not copy {} since destination path is unknown for type {}",
        raw_file_path,
        static_cast<Result::UnderlyingFileTypeInt>(file_type));
  }
}

std::string
ResultRetriever::get_dest_path(FileType file_type) const
{
  switch (file_type) {
  case FileType::object:
    return m_ctx.args_info.output_obj;

  case FileType::dependency:
    if (m_ctx.args_info.generating_dependencies) {
      return m_ctx.args_info.output_dep;
    }
    break;

  case FileType::stdout_output:
  case FileType::stderr_output:
    // Should never get here.
    break;

  case FileType::coverage_unmangled:
    if (m_ctx.args_info.generating_coverage) {
      return Util::change_extension(m_ctx.args_info.output_obj, ".gcno");
    }
    break;

  case FileType::stackusage:
    if (m_ctx.args_info.generating_stackusage) {
      return m_ctx.args_info.output_su;
    }
    break;

  case FileType::diagnostic:
    if (m_ctx.args_info.generating_diagnostics) {
      return m_ctx.args_info.output_dia;
    }
    break;

  case FileType::dwarf_object:
    if (m_ctx.args_info.seen_split_dwarf
        && m_ctx.args_info.output_obj != "/dev/null") {
      return m_ctx.args_info.output_dwo;
    }
    break;

  case FileType::coverage_mangled:
    if (m_ctx.args_info.generating_coverage) {
      return Result::gcno_file_in_mangled_form(m_ctx);
    }
    break;

  case FileType::assembler_listing:
    return m_ctx.args_info.output_al;
  }

  return {};
}

void
ResultRetriever::write_dependency_file(const std::string& path,
                                       nonstd::span<const uint8_t> data)
{
  ASSERT(m_ctx.args_info.dependency_target);

  Fd fd(open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
  if (!fd) {
    throw WriteError(FMT("Failed to open {} for writing", path));
  }

  auto write_data = [&](auto data, auto size) {
    util::throw_on_error<WriteError>(util::write_fd(*fd, data, size),
                                     FMT("Failed to write to {}: ", path));
  };

  std::string_view str_data = util::to_string_view(data);
  size_t start_pos = 0;
  const size_t colon_pos = str_data.find(": ");
  if (colon_pos != std::string::npos) {
    const auto obj_in_dep_file = str_data.substr(0, colon_pos);
    const auto& dep_target = *m_ctx.args_info.dependency_target;
    if (obj_in_dep_file != dep_target) {
      write_data(dep_target.data(), dep_target.length());
      start_pos = colon_pos;
    }
  }

  write_data(str_data.data() + start_pos, str_data.length() - start_pos);
}

} // namespace core
