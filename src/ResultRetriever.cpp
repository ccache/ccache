// Copyright (C) 2020-2021 Joel Rosdahl and other contributors
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

using Result::FileType;

ResultRetriever::ResultRetriever(Context& ctx, bool rewrite_dependency_target)
  : m_ctx(ctx), m_rewrite_dependency_target(rewrite_dependency_target)
{
}

void
ResultRetriever::on_header(CacheEntryReader& /*cache_entry_reader*/)
{
}

void
ResultRetriever::on_entry_start(uint32_t entry_number,
                                FileType file_type,
                                uint64_t file_len,
                                nonstd::optional<std::string> raw_file)
{
  LOG("Reading {} entry #{} {} ({} bytes)",
      raw_file ? "raw" : "embedded",
      entry_number,
      Result::file_type_to_string(file_type),
      file_len);

  std::string dest_path;
  m_dest_file_type = file_type;

  switch (file_type) {
  case FileType::object:
    dest_path = m_ctx.args_info.output_obj;
    break;

  case FileType::dependency:
    // Dependency file: Open destination file but accumulate data in m_dest_data
    // and write it in on_entry_end.
    if (m_ctx.args_info.generating_dependencies) {
      dest_path = m_ctx.args_info.output_dep;
      m_dest_data.reserve(file_len);
    }
    break;

  case FileType::stderr_output:
    // Stderr data: Don't open a destination file. Instead accumulate it in
    // m_dest_data and write it in on_entry_end.
    m_dest_data.reserve(file_len);
    break;

  case FileType::coverage_unmangled:
    if (m_ctx.args_info.generating_coverage) {
      dest_path = Util::change_extension(m_ctx.args_info.output_obj, ".gcno");
    }
    break;

  case FileType::stackusage:
    if (m_ctx.args_info.generating_stackusage) {
      dest_path = m_ctx.args_info.output_su;
    }
    break;

  case FileType::diagnostic:
    if (m_ctx.args_info.generating_diagnostics) {
      dest_path = m_ctx.args_info.output_dia;
    }
    break;

  case FileType::dwarf_object:
    if (m_ctx.args_info.seen_split_dwarf
        && m_ctx.args_info.output_obj != "/dev/null") {
      dest_path = m_ctx.args_info.output_dwo;
    }
    break;

  case FileType::coverage_mangled:
    if (m_ctx.args_info.generating_coverage) {
      dest_path = Result::gcno_file_in_mangled_form(m_ctx);
    }
    break;
  }

  if (file_type == FileType::stderr_output) {
    // Written in on_entry_end.
  } else if (dest_path.empty()) {
    LOG_RAW("Not writing");
  } else if (dest_path == "/dev/null") {
    LOG_RAW("Not writing to /dev/null");
  } else if (raw_file) {
    Util::clone_hard_link_or_copy_file(m_ctx, *raw_file, dest_path, false);

    // Update modification timestamp to save the file from LRU cleanup (and, if
    // hard-linked, to make the object file newer than the source file).
    Util::update_mtime(*raw_file);
  } else {
    LOG("Writing to {}", dest_path);
    m_dest_fd = Fd(
      open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
    if (!m_dest_fd) {
      throw Error(
        "Failed to open {} for writing: {}", dest_path, strerror(errno));
    }
    m_dest_path = dest_path;
  }
}

void
ResultRetriever::on_entry_data(const uint8_t* data, size_t size)
{
  ASSERT((m_dest_file_type == FileType::stderr_output && !m_dest_fd)
         || (m_dest_file_type != FileType::stderr_output && m_dest_fd));

  if (m_dest_file_type == FileType::stderr_output
      || (m_dest_file_type == FileType::dependency && !m_dest_path.empty())) {
    m_dest_data.append(reinterpret_cast<const char*>(data), size);
  } else {
    try {
      Util::write_fd(*m_dest_fd, data, size);
    } catch (Error& e) {
      throw Error("Failed to write to {}: {}", m_dest_path, e.what());
    }
  }
}

void
ResultRetriever::on_entry_end()
{
  if (m_dest_file_type == FileType::stderr_output) {
    LOG("Writing to file descriptor {}", STDERR_FILENO);
    Util::send_to_stderr(m_ctx, m_dest_data);
  } else if (m_dest_file_type == FileType::dependency && !m_dest_path.empty()) {
    write_dependency_file();
  }

  if (m_dest_fd) {
    m_dest_fd.close();
  }
  m_dest_path.clear();
  m_dest_data.clear();
}

void
ResultRetriever::write_dependency_file()
{
  try {
    size_t start_pos = 0;
    if (m_rewrite_dependency_target) {
      size_t colon_pos = m_dest_data.find(':');
      if (colon_pos != std::string::npos) {
        const auto escaped_output_obj =
          Depfile::escape_filename(m_ctx.args_info.output_obj);
        Util::write_fd(
          *m_dest_fd, escaped_output_obj.data(), escaped_output_obj.length());
        start_pos = colon_pos;
      }
    }

    Util::write_fd(*m_dest_fd,
                   m_dest_data.data() + start_pos,
                   m_dest_data.length() - start_pos);
  } catch (Error& e) {
    throw Error("Failed to write to {}: {}", m_dest_path, e.what());
  }
}
