export module HttpServer;

import std;
import ServerRouter;
import TcpSocket;

// A deliberately small HTTP/1.1 server shell: it parses requests into the
// shared `ServerRouter::Request` shape, hands them to a handler (the
// SiteMiddleware), and writes the `ServerRouter::Response` back. One request
// per connection, handled sequentially — the API is a handful of tiny
// SQLite-backed endpoints, so simplicity beats throughput here. All routing
// and validation logic lives in SiteMiddleware, which is what the tests cover;
// this shell is transport only.
export namespace HttpServer {

using Handler = std::function<ServerRouter::Response(const ServerRouter::Request &)>;

// Serves on `port` until `stop` is requested. Returns false if the port could
// not be bound.
bool serve(int port, Handler handler, std::stop_token stop);

} // namespace HttpServer
