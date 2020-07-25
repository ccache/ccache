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

#include "ResultRetriever.hpp"

#include "Context.hpp"
#include "logging.hpp"

#include "third_party/nonstd/string_view.hpp"

using Result::FileType;
using string_view = nonstd::string_view;

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
  std::string dest_path;

  m_dest_file_type = file_type;

  switch (file_type) {
  case FileType::object:
    dest_path = m_ctx.args_info.output_obj;
    break;

  case FileType::dependency:
    if (m_ctx.args_info.generating_dependencies) {
      dest_path = m_ctx.args_info.output_dep;
      m_dest_data.reserve(file_len);
    }
    break;

  case FileType::stderr_output:
    m_dest_data.reserve(file_len);
    return;

  case FileType::coverage:
    if (m_ctx.args_info.generating_coverage) {
      dest_path = m_ctx.args_info.output_cov;
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
  }

  if (dest_path.empty()) {
    cc_log("Not copying");
  } else if (dest_path == "/dev/null") {
    cc_log("Not copying to /dev/null");
  } else {
    cc_log("Retrieving %s file #%u %s (%llu bytes)",
           raw_file ? "raw" : "embedded",
           entry_number,
           Result::file_type_to_string(file_type),
           (unsigned long long)file_len);

    if (raw_file) {
      Util::clone_hard_link_or_copy_file(m_ctx, *raw_file, dest_path, false);

      // Update modification timestamp to save the file from LRU cleanup (and,
      // if hard-linked, to make the object file newer than the source file).
      update_mtime(raw_file->c_str());
    } else {
      cc_log("Copying to %s", dest_path.c_str());
      m_dest_fd = Fd(
        open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
      if (!m_dest_fd) {
        throw Error(fmt::format(
          "Failed to open {} for writing: {}", dest_path, strerror(errno)));
      }
      m_dest_path = dest_path;
    }
  }
}

void
ResultRetriever::on_entry_data(const uint8_t* data, size_t size)
{
  assert((m_dest_file_type == FileType::stderr_output && !m_dest_fd)
         || (m_dest_file_type != FileType::stderr_output && m_dest_fd));

  if (m_dest_file_type == FileType::stderr_output
      || (m_dest_file_type == FileType::dependency && !m_dest_path.empty())) {
    m_dest_data.append(reinterpret_cast<const char*>(data), size);
  } else if (!write_fd(*m_dest_fd, data, size)) {
    throw Error(fmt::format("Failed to write to {}", m_dest_path));
  }
}

void
ResultRetriever::on_entry_end()
{
  if (m_dest_file_type == FileType::stderr_output) {
    Util::send_to_stderr(m_dest_data, m_ctx.args_info.strip_diagnostics_colors);
  } else if (m_dest_file_type == FileType::dependency && !m_dest_path.empty()) {
    write_dependency_file();
  }

  if (m_dest_fd) {
    m_dest_fd.close();
  }

  m_dest_path.clear();
}

void
ResultRetriever::write_dependency_file()
{
  size_t start_pos = 0;

  if (m_rewrite_dependency_target) {
    size_t colon_pos = m_dest_data.find(':');
    if (colon_pos != std::string::npos) {
      if (!write_fd(*m_dest_fd,
                    m_ctx.args_info.output_obj.data(),
                    m_ctx.args_info.output_obj.length())) {
        throw Error(fmt::format("Failed to write to {}", m_dest_path));
      }

      start_pos = colon_pos;
    }
  }

  if (!write_fd(*m_dest_fd,
                m_dest_data.data() + start_pos,
                m_dest_data.length() - start_pos)) {
    throw Error(fmt::format("Failed to write to {}", m_dest_path));
  }
}
