// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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

#include <rpc/msgpack.hpp>

// clang-format off
#include <Digest.hpp>
#include <util/Bytes.hpp>
#include <third_party/nonstd/span.hpp>
// clang-format on

// See <https://github.com/msgpack/msgpack-c/wiki/v2_0_cpp_adaptor>

#define msgpack clmdep_msgpack /* used in rpclib */
namespace msgpack {
MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
{
  namespace adaptor {

  // class Digest (type bin)

  template<> struct convert<Digest>
  {
    msgpack::object const&
    operator()(msgpack::object const& o, Digest& v) const
    {
      if (o.type != msgpack::type::BIN)
        throw msgpack::type_error();
      if (o.via.bin.size != v.size())
        throw msgpack::type_error();
      std::memcpy(v.bytes(), o.via.bin.ptr, v.size());
      return o;
    }
  };

  template<> struct pack<Digest>
  {
    template<typename Stream>
    packer<Stream>&
    operator()(msgpack::packer<Stream>& o, Digest const& v) const
    {
      o.pack_bin(static_cast<uint32_t>(v.size()));
      o.pack_bin_body(reinterpret_cast<const char*>(v.bytes()), v.size());
      return o;
    }
  };

  template<> struct object<Digest>
  {
    void
    operator()(msgpack::object::with_zone& o, Digest const& v) const
    {
      o.type = type::BIN;
      o.via.bin.size = static_cast<uint32_t>(v.size());
      o.via.bin.ptr = reinterpret_cast<const char*>(v.bytes());
    }
  };

  // class util::Bytes (type bin)

  template<> struct convert<util::Bytes>
  {
    msgpack::object const&
    operator()(msgpack::object const& o, util::Bytes& v) const
    {
      if (o.type != msgpack::type::BIN)
        throw msgpack::type_error();
      v = util::Bytes(reinterpret_cast<const uint8_t*>(o.via.bin.ptr),
                      o.via.bin.size);
      return o;
    }
  };

  template<> struct pack<util::Bytes>
  {
    template<typename Stream>
    packer<Stream>&
    operator()(msgpack::packer<Stream>& o, const util::Bytes& v) const
    {
      o.pack_bin(static_cast<uint32_t>(v.size()));
      o.pack_bin_body(reinterpret_cast<const char*>(v.data()), v.size());
      return o;
    }
  };

  template<> struct object<util::Bytes>
  {
    void
    operator()(msgpack::object& o, const util::Bytes& v) const
    {
      o.type = type::BIN;
      o.via.bin.ptr = reinterpret_cast<const char*>(v.data());
      o.via.bin.size = v.size();
    }
  };

  template<> struct object_with_zone<util::Bytes>
  {
    void
    operator()(clmdep_msgpack::object::with_zone& o, const util::Bytes& v) const
    {
      uint32_t size = checked_get_container_size(v.size());
      o.type = type::BIN;
      char* ptr = static_cast<char*>(
        o.zone.allocate_align(size, MSGPACK_ZONE_ALIGNOF(char)));
      o.via.bin.ptr = ptr;
      o.via.bin.size = size;
      std::memcpy(ptr, v.data(), v.size());
    }
  };

  // class nonstd::span<const uint8_t> (type bin)

  template<> struct convert<nonstd::span<const uint8_t>>
  {
    msgpack::object const&
    operator()(msgpack::object const& o, nonstd::span<const uint8_t>& v) const
    {
      if (o.type != msgpack::type::BIN)
        throw msgpack::type_error();
      v = nonstd::span<const uint8_t>(
        reinterpret_cast<const unsigned char*>(o.via.bin.ptr), o.via.bin.size);
      return o;
    }
  };

  template<> struct pack<nonstd::span<const uint8_t>>
  {
    template<typename Stream>
    packer<Stream>&
    operator()(msgpack::packer<Stream>& o,
               nonstd::span<const uint8_t> const& v) const
    {
      o.pack_bin(static_cast<uint32_t>(v.size()));
      o.pack_bin_body(reinterpret_cast<const char*>(v.data()), v.size());
      return o;
    }
  };

  template<> struct object<nonstd::span<const uint8_t>>
  {
    void
    operator()(msgpack::object::with_zone& o,
               nonstd::span<const uint8_t> const& v) const
    {
      o.type = type::BIN;
      o.via.bin.ptr = reinterpret_cast<const char*>(v.data());
      o.via.bin.size = v.size();
    }
  };

  } // namespace adaptor
} // MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
} // namespace msgpack
#undef msgpack
