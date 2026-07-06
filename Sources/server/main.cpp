// The FifteenServer executable — the server side of the monorepo, mirroring
// isowords' `Sources/server`. It boots the environment, serves the HTTP API
// (leaderboard + score submission through SiteMiddleware) and the realtime
// multiplayer referee (GameServer) until interrupted.

#include <signal.h> // C header: safe to mix with `import std` (see App/main.cpp's raylib.h)

import std;
import Dependencies;
import GameServer;
import HttpServer;
import ServerBootstrap;
import SharedModels;
import SiteMiddleware;

using Dependencies::DependencyValues;
using Dependencies::prepareDependencies;

namespace {

// SIGINT/SIGTERM flip a stop source that both server loops watch.
std::stop_source shutdownSource;

void onSignal(int) { shutdownSource.request_stop(); }

} // namespace

int main() {
  // The engine draws room seeds and timestamps through the same controlled
  // dependencies the client uses; resolve them up front so the storage is
  // populated before any connection thread reads it.
  prepareDependencies([](DependencyValues &values) {
    (void)values.get<Dependencies::DateGeneratorKey>();
    (void)values.get<Dependencies::RandomNumberGeneratorKey>();
  });

  const auto environment = ServerBootstrap::bootstrap();
  if (!environment.has_value()) {
    std::println(std::cerr, "fifteen-server: could not open or migrate the database");
    return 1;
  }

  signal(SIGINT, &onSignal);
  signal(SIGTERM, &onSignal);

  std::println("⏳ fifteen-server: http on :{} — multiplayer on :{} — db at {}",
               environment->envVars.httpPort, environment->envVars.multiplayerPort,
               environment->envVars.databasePath);

  // HTTP API on its own thread; the multiplayer referee runs on this one.
  std::jthread http([&](std::stop_token) {
    if (!HttpServer::serve(
            environment->envVars.httpPort,
            [&](const auto &request) {
              return SiteMiddleware::respond(environment->site, request);
            },
            shutdownSource.get_token())) {
      std::println(std::cerr, "fifteen-server: could not bind http port {}",
                   environment->envVars.httpPort);
      shutdownSource.request_stop();
    }
  });

  // Multiplayer winners are server-verified; persist them straight into the
  // same leaderboard the HTTP API serves.
  const bool ok = GameServer::run(
      environment->envVars.multiplayerPort,
      [&](const SharedModels::ScoreSubmission &result) {
        (void)environment->site.database.saveGame(result);
        std::println("🏁 verified multiplayer win: {} ({}x{}, {} moves, {}s)", result.name,
                     result.gridSize, result.gridSize, result.moves, result.duration);
      },
      shutdownSource.get_token());
  if (!ok) {
    std::println(std::cerr, "fifteen-server: could not bind multiplayer port {}",
                 environment->envVars.multiplayerPort);
    shutdownSource.request_stop();
    return 1;
  }

  std::println("fifteen-server: shut down cleanly");
  return 0;
}
