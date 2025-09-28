// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#include <ccache/hash.hpp>
#include <ccache/storage/types.hpp>
#include <ccache/util/bytes.hpp>

#include <cxxurl/url.hpp>
#include <nonstd/span.hpp>
#include <tl/expected.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace storage::remote {

const auto k_default_connect_timeout = std::chrono::milliseconds{100};
const auto k_default_operation_timeout = std::chrono::milliseconds{10000};

// This class defines the API that a remote storage must implement.
class RemoteStorage
{
public:
  class Backend
  {
  public:
    struct Attribute
    {
      std::string key;       // Key part.
      std::string value;     // Value part, percent-decoded.
      std::string raw_value; // Value part, not percent-decoded.
    };

    enum class Failure {
      error,   // Operation error, e.g. bad parameters or failed connection.
      timeout, // Timeout, e.g. due to slow network or server.
    };

    class Failed : public std::runtime_error
    {
    public:
      Failed(Failure failure);
      Failed(const std::string& message, Failure failure = Failure::error);

      Failure failure() const;

    private:
      Failure m_failure;
    };

    virtual ~Backend() = default;

    // Get the value associated with `key`. Returns the value on success or
    // std::nullopt if the entry is not present.
    virtual tl::expected<std::optional<util::Bytes>, Failure>
    get(const Hash::Digest& key) = 0;

    // Put `value` associated to `key` in the storage. Returns true if the entry
    // was stored, otherwise false.
    virtual tl::expected<bool, Failure> put(const Hash::Digest& key,
                                            nonstd::span<const uint8_t> value,
                                            Overwrite overwrite) = 0;

    // Remove `key` and its associated value. Returns true if the entry was
    // removed, otherwise false.
    virtual tl::expected<bool, Failure> remove(const Hash::Digest& key) = 0;

    // Determine whether an attribute is handled by the remote storage
    // framework itself.
    static bool is_framework_attribute(const std::string& name);

    // Parse a timeout `value`, throwing `Failed` on error.
    static std::chrono::milliseconds
    parse_timeout_attribute(const std::string& value);
  };

  virtual ~RemoteStorage() = default;

  // Create an instance of the backend. The instance is created just before the
  // first call to a backend method, so the backend constructor can open a
  // connection or similar right away if wanted. The method should throw
  // `core::Fatal` on fatal configuration error or `Backend::Failed` on
  // connection error or timeout.
  virtual std::unique_ptr<Backend>
  create_backend(const Url& url,
                 const std::vector<Backend::Attribute>& attributes) const = 0;

  // Redact secrets in backend attributes, if any.
  virtual void
  redact_secrets(std::vector<Backend::Attribute>& attributes) const;
};

// --- Inline implementations ---

inline void
RemoteStorage::redact_secrets(
  std::vector<Backend::Attribute>& /*attributes*/) const
{
}

inline RemoteStorage::Backend::Failed::Failed(Failure failure)
  : Failed("", failure)
{
}

inline RemoteStorage::Backend::Failed::Failed(const std::string& message,
                                              Failure failure)
  : std::runtime_error::runtime_error(message),
    m_failure(failure)
{
}

inline RemoteStorage::Backend::Failure
RemoteStorage::Backend::Failed::failure() const
{
  return m_failure;
}

} // namespace storage::remote
