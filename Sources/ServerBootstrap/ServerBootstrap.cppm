export module ServerBootstrap;

import std;
import DatabaseClient;
import DatabaseClientLive;
import SiteMiddleware;

// Boots the server environment — the (much smaller) analog of isowords'
// `ServerBootstrap`: read configuration from the process environment, open the
// database, migrate the schema, and hand back everything the executable needs.
export namespace ServerBootstrap {

struct EnvVars {
  int httpPort = 8080;                                 // FIFTEEN_SERVER_PORT
  int multiplayerPort = 8091;                          // FIFTEEN_SERVER_MP_PORT
  std::string databasePath = "fifteen-server.sqlite3"; // FIFTEEN_SERVER_DATABASE
};

struct Environment {
  EnvVars envVars;
  SiteMiddleware::Environment site;
};

inline EnvVars readEnvVars() {
  EnvVars env;
  const auto readInt = [](const char *name, int fallback) {
    if (const char *value = std::getenv(name); value && *value) {
      int parsed = 0;
      const auto *end = value + std::string_view(value).size();
      if (std::from_chars(value, end, parsed).ec == std::errc{}) {
        return parsed;
      }
    }
    return fallback;
  };
  env.httpPort = readInt("FIFTEEN_SERVER_PORT", env.httpPort);
  env.multiplayerPort = readInt("FIFTEEN_SERVER_MP_PORT", env.multiplayerPort);
  if (const char *path = std::getenv("FIFTEEN_SERVER_DATABASE"); path && *path) {
    env.databasePath = path;
  }
  return env;
}

// Opens (creating if needed) and migrates the database. Nullopt when the
// database cannot be opened or migrated — the server should refuse to boot
// rather than serve errors.
inline std::optional<Environment> bootstrap() {
  const EnvVars envVars = readEnvVars();
  auto database = DatabaseClient::live(envVars.databasePath);
  if (!database.migrate().has_value()) {
    return std::nullopt;
  }
  return Environment{.envVars = envVars, .site = SiteMiddleware::Environment{.database = database}};
}

} // namespace ServerBootstrap
