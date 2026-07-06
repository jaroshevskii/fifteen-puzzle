module;

// JSON stays private to this implementation unit (see ServerRouter).
#include <nlohmann/json.hpp>

module MultiplayerCore; // implementation unit

import std;

namespace MultiplayerCore {

namespace {

using nlohmann::json;

std::string typeOf(const json &doc) { return doc.value("type", ""); }

} // namespace

std::string encode(const ClientMessage &message) {
  return std::visit(
      [](auto &&value) -> std::string {
        using V = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<V, Join>) {
          return json{{"type", "join"}, {"name", value.name}, {"gridSize", value.gridSize}}.dump();
        } else if constexpr (std::is_same_v<V, Move>) {
          return json{{"type", "move"}, {"index", value.index}}.dump();
        } else if constexpr (std::is_same_v<V, Leave>) {
          return json{{"type", "leave"}}.dump();
        }
      },
      message);
}

std::string encode(const ServerMessage &message) {
  return std::visit(
      [](auto &&value) -> std::string {
        using V = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<V, Queued>) {
          return json{{"type", "queued"}}.dump();
        } else if constexpr (std::is_same_v<V, Start>) {
          return json{{"type", "start"},
                      {"seed", value.seed},
                      {"gridSize", value.gridSize},
                      {"opponentName", value.opponentName}}
              .dump();
        } else if constexpr (std::is_same_v<V, OpponentMoved>) {
          return json{
              {"type", "opponentMoved"}, {"index", value.index}, {"moveCount", value.moveCount}}
              .dump();
        } else if constexpr (std::is_same_v<V, MoveRejected>) {
          return json{{"type", "moveRejected"}, {"index", value.index}}.dump();
        } else if constexpr (std::is_same_v<V, Finished>) {
          return json{{"type", "finished"},
                      {"youWon", value.youWon},
                      {"winnerName", value.winnerName},
                      {"durationSeconds", value.durationSeconds},
                      {"moves", value.moves}}
              .dump();
        } else if constexpr (std::is_same_v<V, OpponentLeft>) {
          return json{{"type", "opponentLeft"}}.dump();
        }
      },
      message);
}

std::optional<ClientMessage> decodeClientMessage(std::string_view line) {
  try {
    const json doc = json::parse(line);
    const std::string type = typeOf(doc);
    if (type == "join") {
      return ClientMessage{Join{.name = doc.at("name").get<std::string>(),
                                .gridSize = doc.at("gridSize").get<int>()}};
    }
    if (type == "move") {
      return ClientMessage{Move{.index = doc.at("index").get<int>()}};
    }
    if (type == "leave") {
      return ClientMessage{Leave{}};
    }
    return std::nullopt;
  } catch (const json::exception &) {
    return std::nullopt;
  }
}

std::optional<ServerMessage> decodeServerMessage(std::string_view line) {
  try {
    const json doc = json::parse(line);
    const std::string type = typeOf(doc);
    if (type == "queued") {
      return ServerMessage{Queued{}};
    }
    if (type == "start") {
      return ServerMessage{Start{.seed = doc.at("seed").get<std::uint64_t>(),
                                 .gridSize = doc.at("gridSize").get<int>(),
                                 .opponentName = doc.at("opponentName").get<std::string>()}};
    }
    if (type == "opponentMoved") {
      return ServerMessage{OpponentMoved{.index = doc.at("index").get<int>(),
                                         .moveCount = doc.at("moveCount").get<int>()}};
    }
    if (type == "moveRejected") {
      return ServerMessage{MoveRejected{.index = doc.at("index").get<int>()}};
    }
    if (type == "finished") {
      return ServerMessage{Finished{.youWon = doc.at("youWon").get<bool>(),
                                    .winnerName = doc.at("winnerName").get<std::string>(),
                                    .durationSeconds = doc.at("durationSeconds").get<int>(),
                                    .moves = doc.at("moves").get<int>()}};
    }
    if (type == "opponentLeft") {
      return ServerMessage{OpponentLeft{}};
    }
    return std::nullopt;
  } catch (const json::exception &) {
    return std::nullopt;
  }
}

} // namespace MultiplayerCore
