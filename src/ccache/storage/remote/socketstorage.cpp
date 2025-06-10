#include "socketstorage.hpp"

#include "ccache/hash.hpp"
#include "ccache/storage/remote/remotestorage.hpp"
#include "ccache/util/logging.hpp"
#include "ccache/util/string.hpp"
#include "socketbackend/launcher.hpp"
#include "socketbackend/messenger.hpp"

#include <cxxurl/url.hpp>
#include <fmt/base.h>
#include <httplib.h>
#include <nonstd/span.hpp>
#include <tl/expected.hpp>

#include <sys/types.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace storage::remote {

class BackendNode : public RemoteStorage::Backend
{
public:
  BackendNode(const Url& url, const std::string& name, std::string attributes);

  tl::expected<std::optional<util::Bytes>, RemoteStorage::Backend::Failure>
  get(const Hash::Digest& key) override;

  tl::expected<bool, RemoteStorage::Backend::Failure>
  put(const Hash::Digest& key,
      nonstd::span<const uint8_t> value,
      bool only_if_missing = false) override;

  tl::expected<bool, RemoteStorage::Backend::Failure>
  remove(const Hash::Digest& key) override;

  // The Unix domain socket used for IPC with the backend service.
  std::unique_ptr<UnixSocket> bsock;

  // The messaging handler object responsible for managing message passing.
  std::unique_ptr<msgr::MessageHandler> m_msg_handler;
};

std::unique_ptr<RemoteStorage::Backend>
SocketStorage::create_backend(
  const Url& url,
  const std::vector<RemoteStorage::Backend::Attribute>& attributes) const
{
  // remove the socket+ prefix from the url
  Url real_url(url.str().substr(7));

  // Unix socket named something like ${TEMPDIR}/backend-<unique hash>.sock
  Hash name = Hash{};
  std::string attr_str;
  name.hash(real_url.str());
  for (const auto& a : attributes) {
    attr_str += "|" + a.key + "=" + a.raw_value;
    name.hash(a.key);
    name.hash(a.value);
    name.hash(a.raw_value);
  }

  auto rbackend = std::make_unique<BackendNode>(
    real_url, util::format_base16(name.digest()), attr_str);

  if (setup_backend_service(*rbackend->bsock) < 1) {
    return nullptr;
  }
  return std::move(rbackend);
}

int
SocketStorage::setup_backend_service(UnixSocket& sock) const
{
  msgr::MessageHandler msg_handler;
  std::vector<uint8_t> transaction_result;
  msg_handler.create(MSG_TEST, std::array<uint8_t, 3>{1, 2, 3});
  auto status = msg_handler.dispatch(transaction_result, sock);

  if (status.has_value()) {
    LOG("DEBUG {} message went wrong!", MSG_TEST);
    return INVALID_SOCKET;
  }

  return 1;
}

BackendNode::BackendNode(const Url& url,
                         const std::string& name,
                         std::string attributes)
  : bsock(std::make_unique<UnixSocket>(name, ' ')),
    m_msg_handler(std::make_unique<msgr::MessageHandler>())
{
  fs::path sock_path = bsock->generate_path();
  if (!bsock->exists()) {
    LOG("DEBUG Socket on {} does not seem to exist!",
        sock_path.generic_string());
    backend::start_daemon(
      url.scheme(), sock_path, url.str() + attributes, BUFFERSIZE);
    sleep(1); // wait a second
  }

  if (!bsock->start(false)) {
    LOG("DEBUG socket {} in use but process dead!", sock_path.generic_string());
    backend::start_daemon(
      url.scheme(), sock_path, url.str() + attributes, BUFFERSIZE);
    sleep(1); // wait a second
  }
}

tl::expected<std::optional<util::Bytes>, RemoteStorage::Backend::Failure>
BackendNode::get(const Hash::Digest& key)
{
  std::vector<uint8_t> transaction_result;
  m_msg_handler->create(MSG_GET, key);
  auto status = m_msg_handler->dispatch(transaction_result, *bsock);

  if (status.has_value()) {
    LOG("{} occured on sending message! ",
        (int(*status) ? "ERROR" : "TIMEOUT"));
    return tl::unexpected<RemoteStorage::Backend::Failure>(*status);
  }

  if (transaction_result.empty()) {
    return std::nullopt;
  }

  return util::Bytes(transaction_result.data(), transaction_result.size());
}

tl::expected<bool, RemoteStorage::Backend::Failure>
BackendNode::put(const Hash::Digest& key,
                 nonstd::span<const uint8_t> value,
                 bool only_if_missing)
{
  std::vector<uint8_t> transaction_result;
  m_msg_handler->create(MSG_PUT, key, value, only_if_missing);
  auto status = m_msg_handler->dispatch(transaction_result, *bsock);

  if (status.has_value()) {
    LOG("{} occured on sending message! ",
        (int(*status) ? "ERROR" : "TIMEOUT"));
    return tl::unexpected<RemoteStorage::Backend::Failure>(*status);
  }

  if (transaction_result.empty()) {
    return tl::unexpected<RemoteStorage::Backend::Failure>(
      RemoteStorage::Backend::Failure::error);
  }

  return true;
}

tl::expected<bool, RemoteStorage::Backend::Failure>
BackendNode::remove(const Hash::Digest& key)
{
  std::vector<uint8_t> transaction_result;
  m_msg_handler->create(MSG_RM, key);
  auto status = m_msg_handler->dispatch(transaction_result, *bsock);

  if (status.has_value()) {
    LOG("{} occured on sending message! ",
        (int(*status) ? "ERROR" : "TIMEOUT"));
    return tl::unexpected<RemoteStorage::Backend::Failure>(*status);
  }

  if (transaction_result.empty()) {
    return tl::unexpected<RemoteStorage::Backend::Failure>(
      RemoteStorage::Backend::Failure::error);
  }
  return true;
}

} // namespace storage::remote
