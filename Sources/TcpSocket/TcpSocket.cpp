module;

// Platform socket headers live in this implementation unit's global module
// fragment, so they never reach importers (winsock in particular drags in
// <windows.h>, which must not meet `import std` in a reachable interface).
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

module TcpSocket; // implementation unit

import std;

namespace TcpSocket {

namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr std::intptr_t kInvalid = static_cast<std::intptr_t>(INVALID_SOCKET);

// Process-wide Winsock init, done lazily before the first socket call.
void ensureStartup() {
  static const int once = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data);
  }();
  (void)once;
}

void closeNative(NativeSocket s) { closesocket(s); }

bool wouldBlock() { return WSAGetLastError() == WSAETIMEDOUT; }

void setRecvTimeout(NativeSocket s, std::chrono::milliseconds timeout) {
  const DWORD value = static_cast<DWORD>(timeout.count());
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&value), sizeof(value));
}
#else
using NativeSocket = int;
constexpr std::intptr_t kInvalid = -1;

void ensureStartup() {}

void closeNative(NativeSocket s) { ::close(s); }

bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }

void setRecvTimeout(NativeSocket s, std::chrono::milliseconds timeout) {
  timeval value{};
  value.tv_sec = static_cast<decltype(value.tv_sec)>(timeout.count() / 1000);
  value.tv_usec = static_cast<decltype(value.tv_usec)>((timeout.count() % 1000) * 1000);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
}
#endif

NativeSocket native(std::intptr_t handle) { return static_cast<NativeSocket>(handle); }

// Writing to a socket whose peer has closed raises SIGPIPE on POSIX, whose
// default action terminates the process — so a client that connects and drops
// abruptly (a readiness probe, a killed peer) would take the whole server (or
// the game) down. Suppress it per socket: `MSG_NOSIGNAL` on the send path where
// available (Linux), and the `SO_NOSIGPIPE` socket option where that is the
// mechanism instead (macOS/BSD). Windows has no SIGPIPE, so both compile out.
#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void suppressSigpipe([[maybe_unused]] NativeSocket s) {
#if defined(SO_NOSIGPIPE)
  const int on = 1;
  setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char *>(&on), sizeof(on));
#endif
}

} // namespace

// --- Connection ----------------------------------------------------------

Connection::Connection(Connection &&other) noexcept
    : handle_(std::exchange(other.handle_, kInvalid)), buffer_(std::move(other.buffer_)) {}

Connection &Connection::operator=(Connection &&other) noexcept {
  if (this != &other) {
    close();
    handle_ = std::exchange(other.handle_, kInvalid);
    buffer_ = std::move(other.buffer_);
  }
  return *this;
}

Connection::~Connection() { close(); }

std::optional<Connection> Connection::connect(const std::string &host, int port) {
  ensureStartup();

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *results = nullptr;
  const std::string service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &results) != 0) {
    return std::nullopt;
  }

  std::intptr_t handle = kInvalid;
  for (const addrinfo *info = results; info != nullptr; info = info->ai_next) {
    const NativeSocket s = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (static_cast<std::intptr_t>(s) == kInvalid) {
      continue;
    }
    suppressSigpipe(s);
    if (::connect(s, info->ai_addr, static_cast<int>(info->ai_addrlen)) == 0) {
      handle = static_cast<std::intptr_t>(s);
      break;
    }
    closeNative(s);
  }
  freeaddrinfo(results);

  if (handle == kInvalid) {
    return std::nullopt;
  }
  return Connection(handle);
}

void Connection::setReceiveTimeout(std::chrono::milliseconds timeout) {
  if (valid()) {
    setRecvTimeout(native(handle_), timeout);
  }
}

bool Connection::sendAll(std::string_view data) {
  if (!valid()) {
    return false;
  }
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto n = ::send(native(handle_), data.data() + sent,
#if defined(_WIN32)
                          static_cast<int>(data.size() - sent),
#else
                          data.size() - sent,
#endif
                          kSendFlags);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

LineRead Connection::readLine() {
  if (!valid()) {
    return LineRead{ReadStatus::closed, {}};
  }
  while (true) {
    if (const std::size_t newline = buffer_.find('\n'); newline != std::string::npos) {
      std::string line = buffer_.substr(0, newline);
      buffer_.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      return LineRead{ReadStatus::line, std::move(line)};
    }

    char chunk[1024];
    const auto n = ::recv(native(handle_), chunk, sizeof(chunk), 0);
    if (n > 0) {
      buffer_.append(chunk, static_cast<std::size_t>(n));
      continue;
    }
    if (n < 0 && wouldBlock()) {
      return LineRead{ReadStatus::timedOut, {}};
    }
    return LineRead{ReadStatus::closed, {}};
  }
}

std::optional<std::string> Connection::readExact(std::size_t count) {
  if (!valid()) {
    return std::nullopt;
  }
  while (buffer_.size() < count) {
    char chunk[1024];
    const auto n = ::recv(native(handle_), chunk, sizeof(chunk), 0);
    if (n > 0) {
      buffer_.append(chunk, static_cast<std::size_t>(n));
      continue;
    }
    if (n < 0 && wouldBlock()) {
      continue; // exact reads ride out timeouts (bodies are short)
    }
    return std::nullopt;
  }
  std::string result = buffer_.substr(0, count);
  buffer_.erase(0, count);
  return result;
}

void Connection::close() {
  if (valid()) {
    closeNative(native(handle_));
    handle_ = kInvalid;
  }
  buffer_.clear();
}

// --- Listener --------------------------------------------------------------

Listener::Listener(Listener &&other) noexcept : handle_(std::exchange(other.handle_, kInvalid)) {}

Listener &Listener::operator=(Listener &&other) noexcept {
  if (this != &other) {
    close();
    handle_ = std::exchange(other.handle_, kInvalid);
  }
  return *this;
}

Listener::~Listener() { close(); }

std::optional<Listener> Listener::bind(int port) {
  ensureStartup();

  const NativeSocket s = socket(AF_INET, SOCK_STREAM, 0);
  if (static_cast<std::intptr_t>(s) == kInvalid) {
    return std::nullopt;
  }

  const int enable = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&enable), sizeof(enable));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::bind(s, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
      ::listen(s, 16) != 0) {
    closeNative(s);
    return std::nullopt;
  }

  Listener listener;
  listener.handle_ = static_cast<std::intptr_t>(s);
  return listener;
}

std::optional<Connection> Listener::accept() {
  if (!valid()) {
    return std::nullopt;
  }
  const NativeSocket s = ::accept(native(handle_), nullptr, nullptr);
  if (static_cast<std::intptr_t>(s) == kInvalid) {
    return std::nullopt;
  }
  suppressSigpipe(s);
  return Connection(static_cast<std::intptr_t>(s));
}

void Listener::close() {
  if (valid()) {
    closeNative(native(handle_));
    handle_ = kInvalid;
  }
}

} // namespace TcpSocket
