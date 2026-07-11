#!/bin/sh
# Runs FifteenServer. Works both in a release archive (a FifteenServer binary
# sits next to this script) and in the dev tree (build it first, then run).
# No Docker needed — the server is a single binary with an SQLite file beside it.
#
#   ./run-server.sh                              # http :8080, multiplayer :8091
#   FIFTEEN_SERVER_PORT=18080 FIFTEEN_SERVER_MP_PORT=18091 ./run-server.sh
#
# Point the game at it (another machine) with, e.g.:
#   FIFTEEN_API_BASE_URL=http://HOST_IP:8080 FIFTEEN_MP_HOST=HOST_IP ./FifteenPuzzle
set -eu
here="$(cd "$(dirname "$0")" && pwd)"

if [ -x "$here/FifteenServer" ]; then
  # Release archive: the binary is right next to this script.
  : "${FIFTEEN_SERVER_DATABASE:=$here/fifteen-server.sqlite3}"
  export FIFTEEN_SERVER_DATABASE
  exec "$here/FifteenServer"
elif [ -x "$here/../build/FifteenServer" ]; then
  # Dev tree: use an already-built binary.
  : "${FIFTEEN_SERVER_DATABASE:=$here/../build/fifteen-server.sqlite3}"
  export FIFTEEN_SERVER_DATABASE
  exec "$here/../build/FifteenServer"
else
  # Dev tree, not built yet: build it, then run.
  cd "$here/.."
  cmake --preset macos
  cmake --build --preset macos --target FifteenServer
  : "${FIFTEEN_SERVER_DATABASE:=build/fifteen-server.sqlite3}"
  export FIFTEEN_SERVER_DATABASE
  exec ./build/FifteenServer
fi
