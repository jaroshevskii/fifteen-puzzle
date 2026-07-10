export module MultiplayerClientLive;

import std;
import MultiplayerClient;
import MultiplayerCore;
import TcpSocket;

// Live, TCP-backed implementation of the multiplayer connection. Wire it in at
// app launch:
//
//   prepareDependencies([](DependencyValues &values) {
//     values.set<MultiplayerClient::Key>(MultiplayerClient::live());
//   });
export namespace MultiplayerClient {

// Returns a client that connects to the FifteenServer multiplayer port.
// Defaults to localhost:8091; override with FIFTEEN_MP_HOST / FIFTEEN_MP_PORT
// (the ports mirror the server's FIFTEEN_SERVER_MP_PORT).
Client live(std::string explicitHost = {}, int explicitPort = 0);

} // namespace MultiplayerClient

namespace MultiplayerClient {

namespace {

// The one active connection, shared between the blocking `connect` loop and
// `sendMove` calls arriving from the store thread.
struct Session {
  std::mutex mutex;
  TcpSocket::Connection *connection = nullptr; // owned by the connect loop's stack
};

std::string resolveHost(const std::string &explicitHost) {
  if (!explicitHost.empty()) {
    return explicitHost;
  }
  if (const char *env = std::getenv("FIFTEEN_MP_HOST"); env && *env) {
    return env;
  }
  return "localhost";
}

int resolvePort(int explicitPort) {
  if (explicitPort > 0) {
    return explicitPort;
  }
  if (const char *env = std::getenv("FIFTEEN_MP_PORT"); env && *env) {
    int parsed = 0;
    const auto *end = env + std::string_view(env).size();
    if (std::from_chars(env, end, parsed).ec == std::errc{} && parsed > 0) {
      return parsed;
    }
  }
  return 8091;
}

} // namespace

Client live(std::string explicitHost, int explicitPort) {
  const std::string host = resolveHost(explicitHost);
  const int port = resolvePort(explicitPort);
  auto session = std::make_shared<Session>();

  return Client{
      .connect =
          [host, port, session](std::string name, int gridSize, std::function<void(Event)> onEvent,
                                std::stop_token stop) {
            auto connection = TcpSocket::Connection::connect(host, port);
            if (!connection.has_value()) {
              onEvent(Failed{});
              return;
            }
            {
              std::scoped_lock lock(session->mutex);
              session->connection = &*connection;
            }
            // A short receive timeout keeps the read loop responsive to
            // cancellation (the portable answer to interrupting recv()).
            connection->setReceiveTimeout(std::chrono::milliseconds(100));
            connection->sendAll(
                MultiplayerCore::encode(MultiplayerCore::ClientMessage{
                    MultiplayerCore::Join{.name = std::move(name), .gridSize = gridSize}}) +
                "\n");
            onEvent(Connected{});

            bool closedByServer = false;
            while (!stop.stop_requested()) {
              const auto read = connection->readLine();
              if (read.status == TcpSocket::ReadStatus::timedOut) {
                continue;
              }
              if (read.status == TcpSocket::ReadStatus::closed) {
                closedByServer = true;
                break;
              }
              if (const auto message = MultiplayerCore::decodeServerMessage(read.line)) {
                onEvent(Received{*message});
              }
            }

            {
              std::scoped_lock lock(session->mutex);
              session->connection = nullptr;
            }
            if (closedByServer) {
              onEvent(Closed{});
            } else {
              // Cancelled locally: tell the server we are leaving, politely.
              connection->sendAll(MultiplayerCore::encode(
                                      MultiplayerCore::ClientMessage{MultiplayerCore::Leave{}}) +
                                  "\n");
            }
            connection->close();
          },
      .sendMove =
          [session](int index) {
            std::scoped_lock lock(session->mutex);
            if (session->connection != nullptr) {
              session->connection->sendAll(MultiplayerCore::encode(MultiplayerCore::ClientMessage{
                                               MultiplayerCore::Move{.index = index}}) +
                                           "\n");
            }
          },
      .observe =
          [host, port](std::function<void(Event)> onEvent, std::stop_token stop) {
            auto connection = TcpSocket::Connection::connect(host, port);
            if (!connection.has_value()) {
              onEvent(Failed{});
              return;
            }
            connection->setReceiveTimeout(std::chrono::milliseconds(100));
            connection->sendAll(MultiplayerCore::encode(
                                    MultiplayerCore::ClientMessage{MultiplayerCore::Observe{}}) +
                                "\n");
            onEvent(Connected{});

            bool closedByServer = false;
            while (!stop.stop_requested()) {
              const auto read = connection->readLine();
              if (read.status == TcpSocket::ReadStatus::timedOut) {
                continue;
              }
              if (read.status == TcpSocket::ReadStatus::closed) {
                closedByServer = true;
                break;
              }
              if (const auto message = MultiplayerCore::decodeServerMessage(read.line)) {
                onEvent(Received{*message});
              }
            }
            if (closedByServer) {
              onEvent(Closed{});
            } else {
              connection->sendAll(MultiplayerCore::encode(
                                      MultiplayerCore::ClientMessage{MultiplayerCore::Leave{}}) +
                                  "\n");
            }
            connection->close();
          },
  };
}

} // namespace MultiplayerClient
