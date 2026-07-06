module HttpServer; // implementation unit

import std;
import ServerRouter;
import TcpSocket;

namespace HttpServer {

namespace {

std::string_view statusText(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 500:
    return "Internal Server Error";
  default:
    return "OK";
  }
}

// Parses "GET /path?query HTTP/1.1" + headers + optional Content-Length body.
std::optional<ServerRouter::Request> readRequest(TcpSocket::Connection &connection) {
  const auto requestLine = connection.readLine();
  if (requestLine.status != TcpSocket::ReadStatus::line) {
    return std::nullopt;
  }

  std::istringstream stream(requestLine.line);
  std::string method, target, version;
  stream >> method >> target >> version;
  if (method.empty() || target.empty()) {
    return std::nullopt;
  }

  ServerRouter::Request request;
  request.method = method;
  if (const std::size_t question = target.find('?'); question != std::string::npos) {
    request.path = target.substr(0, question);
    request.query = target.substr(question + 1);
  } else {
    request.path = target;
  }

  std::size_t contentLength = 0;
  while (true) {
    const auto header = connection.readLine();
    if (header.status != TcpSocket::ReadStatus::line) {
      return std::nullopt;
    }
    if (header.line.empty()) {
      break; // end of headers
    }
    const std::size_t colon = header.line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string name = header.line.substr(0, colon);
    std::ranges::transform(name, name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "content-length") {
      std::string_view value = std::string_view(header.line).substr(colon + 1);
      while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
      }
      std::size_t length = 0;
      const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), length);
      if (ec != std::errc{}) {
        return std::nullopt;
      }
      contentLength = length;
    }
  }

  if (contentLength > 1'000'000) {
    return std::nullopt; // no legitimate request body is this large
  }
  if (contentLength > 0) {
    auto body = connection.readExact(contentLength);
    if (!body.has_value()) {
      return std::nullopt;
    }
    request.body = std::move(*body);
  }
  return request;
}

void writeResponse(TcpSocket::Connection &connection, const ServerRouter::Response &response) {
  const std::string head = std::format(
      "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\n"
      "Connection: close\r\n\r\n",
      response.status, statusText(response.status), response.contentType, response.body.size());
  connection.sendAll(head);
  connection.sendAll(response.body);
}

} // namespace

bool serve(int port, Handler handler, std::stop_token stop) {
  auto listener = TcpSocket::Listener::bind(port);
  if (!listener.has_value()) {
    return false;
  }

  // Closing the listener from the stop callback unblocks the pending accept().
  std::stop_callback unblock(stop, [&listener] { listener->close(); });

  while (!stop.stop_requested()) {
    auto connection = listener->accept();
    if (!connection.has_value()) {
      continue; // listener closed (shutdown) or transient accept failure
    }
    connection->setReceiveTimeout(std::chrono::seconds(5));
    if (auto request = readRequest(*connection)) {
      writeResponse(*connection, handler(*request));
    } else {
      writeResponse(*connection, ServerRouter::Response{.status = 400, .body = "{}"});
    }
    connection->close();
  }
  return true;
}

} // namespace HttpServer
