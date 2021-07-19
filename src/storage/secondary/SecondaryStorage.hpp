// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#pragma once

#include <storage/types.hpp>

#include <third_party/nonstd/expected.hpp>
#include <third_party/nonstd/optional.hpp>
#include <third_party/url.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class Digest;

namespace storage {
namespace secondary {

constexpr auto k_redacted_password = "********";
const auto k_default_connect_timeout = std::chrono::milliseconds{100};
const auto k_default_operation_timeout = std::chrono::milliseconds{10000};

// This class defines the API that a secondary storage must implement.
class SecondaryStorage
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

    struct Params
    {
      Url url;
      std::vector<Attribute> attributes;
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
    // nonstd::nullopt if the entry is not present.
    virtual nonstd::expected<nonstd::optional<std::string>, Failure>
    get(const Digest& key) = 0;

    // Put `value` associated to `key` in the storage. A true `only_if_missing`
    // is a hint that the value does not have to be set if already present.
    // Returns true if the entry was stored, otherwise false.
    virtual nonstd::expected<bool, Failure>
    put(const Digest& key,
        const std::string& value,
        bool only_if_missing = false) = 0;

    // Remove `key` and its associated value. Returns true if the entry was
    // removed, otherwise false.
    virtual nonstd::expected<bool, Failure> remove(const Digest& key) = 0;

    // Determine whether an attribute is handled by the secondary storage
    // framework itself.
    static bool is_framework_attribute(const std::string& name);

    // Parse a timeout `value`, throwing `Failed` on error.
    static std::chrono::milliseconds
    parse_timeout_attribute(const std::string& value);
  };

  virtual ~SecondaryStorage() = default;

  // Create an instance of the backend. The instance is created just before the
  // first call to a backend method, so the backend constructor can open a
  // connection or similar right away if wanted. The method should throw
  // `core::Fatal` on fatal configuration error or `Backend::Failed` on
  // connection error or timeout.
  virtual std::unique_ptr<Backend>
  create_backend(const Backend::Params& parameters) const = 0;

  // Redact secrets in backend parameters, if any.
  virtual void redact_secrets(Backend::Params& parameters) const;
};

// --- Inline implementations ---

inline void
SecondaryStorage::redact_secrets(
  SecondaryStorage::Backend::Params& /*config*/) const
{
}

inline SecondaryStorage::Backend::Failed::Failed(Failure failure)
  : Failed("", failure)
{
}

inline SecondaryStorage::Backend::Failed::Failed(const std::string& message,
                                                 Failure failure)
  : std::runtime_error::runtime_error(message),
    m_failure(failure)
{
}

inline SecondaryStorage::Backend::Failure
SecondaryStorage::Backend::Failed::failure() const
{
  return m_failure;
}

} // namespace secondary
} // namespace storage
