#!/bin/sh
# Builds and runs the FifteenServer locally (the analog of isowords'
# Bootstrap/ dev setup — no Docker needed, the server is a single binary with
# an SQLite file next to it).
#
#   Bootstrap/run-server.sh              # http :8080, multiplayer :8091
#   FIFTEEN_SERVER_PORT=9000 Bootstrap/run-server.sh
#
# Point the game at it with:
#   FIFTEEN_API_BASE_URL=http://localhost:8080 FIFTEEN_MP_PORT=8091 ./build/FifteenPuzzle
set -eu
cd "$(dirname "$0")/.."

cmake --preset macos
cmake --build --preset macos --target FifteenServer

: "${FIFTEEN_SERVER_DATABASE:=build/fifteen-server.sqlite3}"
export FIFTEEN_SERVER_DATABASE

exec ./build/FifteenServer
