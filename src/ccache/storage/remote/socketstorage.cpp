#include "socketstorage.hpp"

#include "ccache/hash.hpp"
#include "ccache/storage/remote/remotestorage.hpp"
#include "ccache/storage/remote/socketbackend/tlv_constants.hpp"
#include "ccache/storage/remote/socketbackend/tlv_protocol.hpp"
#include "ccache/util/bytes.hpp"
#include "ccache/util/logging.hpp"
#include "ccache/util/string.hpp"
#include "socketbackend/launcher.hpp"

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
#include <utility>
#include <vector>

namespace storage::remote {

constexpr std::chrono::seconds STARTUP_DELAY(5);

class BackendNode : public RemoteStorage::Backend
{
public:
  BackendNode(const Url& url,
              const std::string& name,
              const std::vector<RemoteStorage::Backend::Attribute>& attributes);

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
  name.hash(real_url.str());
  for (const auto& a : attributes) {
    name.hash(a.key);
    name.hash(a.value);
    name.hash(a.raw_value);
  }

  auto rbackend = std::make_unique<BackendNode>(
    real_url, util::format_base16(name.digest()), attributes);

  if (setup_backend_service(*rbackend->bsock) < 1) {
    return nullptr;
  }
  return std::move(rbackend);
}

int
SocketStorage::setup_backend_service(UnixSocket& sock) const
{
  std::vector<uint8_t> transaction_result;
  auto status = tlv::dispatch(transaction_result,
                              sock,
                              tlv::MSG_TYPE_SETUP_REQUEST,
                              std::array<uint8_t, 3>{1, 2, 3});

  if (status != tlv::SUCCESS) {
    LOG("DEBUG msg_type_notify {} message went wrong!",
        tlv::MSG_TYPE_SETUP_REQUEST);
    return INVALID_SOCKET;
  }

  return 1;
}

BackendNode::BackendNode(
  const Url& url,
  const std::string& name,
  const std::vector<RemoteStorage::Backend::Attribute>& attributes)
  : bsock(std::make_unique<UnixSocket>(name, 0xFF))
{
  if (bsock->start(false)) {
    return;
  }

  fs::path sock_path = bsock->generate_path();
  fs::path lock_path = sock_path.string() + ".lock";

  int lock_fd = open(lock_path.c_str(), O_RDWR | O_CREAT, 0666);
  if (lock_fd < 0) {
    throw std::runtime_error("Failed to open lock file: " + lock_path.string());
  }

  bool daemon_started = false;
  util::FileLock lock(lock_fd);

  if (lock.acquire()) {
    if (bsock->start(false)) {
      lock.release();
      close(lock_fd);
      return;
    }

    LOG("Process {} creating socket on {}.",
        getpid(),
        sock_path.generic_string());

    if (bsock->exists()) {
      fs::remove(sock_path);
    }

    backend::start_daemon(
      url.scheme(), sock_path, url.str(), attributes, BUFFERSIZE);

    daemon_started = true;
  } else {
    LOG("Process {} is waiting for others to intialise daemon", getpid());
    // just be a tiny bit patient
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  auto start_time = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start_time < STARTUP_DELAY) {
    if (bsock->exists() && bsock->start(false)) {
      lock.release();
      close(lock_fd);
      fs::remove(lock_path); // this can be parallelised
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (daemon_started) {
    LOG("ERROR: Daemon started but socket not available after timeout", "");
  } else {
    LOG("ERROR: Timed out waiting for other process to create socket", "");
  }

  lock.release();
  close(lock_fd);        // failed to intialise!
  fs::remove(lock_path); // this can be parallelised
  assert(false && "Failed to initialize backend node");
}

tl::expected<std::optional<util::Bytes>, RemoteStorage::Backend::Failure>
BackendNode::get(const Hash::Digest& key)
{
  std::vector<uint8_t> transaction_result;
  auto status =
    tlv::dispatch(transaction_result, *bsock, tlv::MSG_TYPE_GET_REQUEST, key);

  if (status != tlv::SUCCESS) {
    LOG("{} occured on sending message!",
        (status == tlv::ERROR ? "ERROR" : "TIMEOUT"));
    return tl::unexpected<RemoteStorage::Backend::Failure>(
      status == tlv::ERROR ? RemoteStorage::Backend::Failure::error
                           : RemoteStorage::Backend::Failure::timeout);
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
  auto status = tlv::dispatch(transaction_result,
                              *bsock,
                              tlv::MSG_TYPE_PUT_REQUEST,
                              key,
                              value,
                              only_if_missing);

  if (status != tlv::SUCCESS) {
    LOG("{} occured on sending message! ",
        (status == tlv::ERROR ? "ERROR" : "TIMEOUT"));
    return tl::unexpected<RemoteStorage::Backend::Failure>(
      status == tlv::ERROR ? RemoteStorage::Backend::Failure::error
                           : RemoteStorage::Backend::Failure::timeout);
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
  auto status =
    tlv::dispatch(transaction_result, *bsock, tlv::MSG_TYPE_DEL_REQUEST, key);

  if (status != tlv::SUCCESS) {
    LOG("{} occured on sending message! ",
        (status == tlv::ERROR ? "ERROR" : "TIMEOUT"));
    return tl::unexpected<RemoteStorage::Backend::Failure>(
      status == tlv::ERROR ? RemoteStorage::Backend::Failure::error
                           : RemoteStorage::Backend::Failure::timeout);
  }

  if (transaction_result.empty()) {
    return tl::unexpected<RemoteStorage::Backend::Failure>(
      RemoteStorage::Backend::Failure::error);
  }
  return true;
}

} // namespace storage::remote
