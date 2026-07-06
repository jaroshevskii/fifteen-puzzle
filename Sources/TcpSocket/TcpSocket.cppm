export module TcpSocket;

import std;

// A minimal, blocking TCP wrapper shared by the transport shells: HttpServer
// and GameServer on the server side, MultiplayerClientLive on the client side.
// The platform socket headers (POSIX / Winsock) are confined to the
// implementation unit, so this interface stays clean for `import std`.
//
// Reads support a receive timeout so callers can poll a `std::stop_token`
// between attempts — the portable way to make a blocking read cancellable.
export namespace TcpSocket {

enum class ReadStatus : std::uint8_t {
  line,     // a full line was read (returned without the trailing '\n')
  timedOut, // no full line within the receive timeout; try again
  closed,   // peer closed the connection (or a transport error)
};

struct LineRead {
  ReadStatus status = ReadStatus::closed;
  std::string line;
};

class Connection {
public:
  Connection() = default;
  Connection(Connection &&other) noexcept;
  Connection &operator=(Connection &&other) noexcept;
  Connection(const Connection &) = delete;
  Connection &operator=(const Connection &) = delete;
  ~Connection();

  // Connects to host:port (IPv4/IPv6, name resolution included).
  static std::optional<Connection> connect(const std::string &host, int port);

  bool valid() const { return handle_ >= 0; }

  // Maximum time a read waits before reporting `timedOut`. 0 = block forever.
  void setReceiveTimeout(std::chrono::milliseconds timeout);

  bool sendAll(std::string_view data); // false on transport error

  // Reads up to the next '\n' (buffering across calls, so a line split over
  // packets — or over timeouts — is assembled correctly).
  LineRead readLine();

  // Reads exactly `count` bytes (for HTTP bodies). Nullopt on EOF/error.
  std::optional<std::string> readExact(std::size_t count);

  void close();

private:
  friend class Listener;
  explicit Connection(std::intptr_t handle) : handle_(handle) {}

  std::intptr_t handle_ = -1;
  std::string buffer_;
};

class Listener {
public:
  Listener() = default;
  Listener(Listener &&other) noexcept;
  Listener &operator=(Listener &&other) noexcept;
  Listener(const Listener &) = delete;
  Listener &operator=(const Listener &) = delete;
  ~Listener();

  // Binds and listens on all interfaces.
  static std::optional<Listener> bind(int port);

  bool valid() const { return handle_ >= 0; }

  // Blocks until a client connects. Nullopt when the listener was closed.
  std::optional<Connection> accept();

  void close(); // also unblocks a pending accept()

private:
  std::intptr_t handle_ = -1;
};

} // namespace TcpSocket
