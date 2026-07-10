#!/usr/bin/env python3
"""End-to-end smoke test for FifteenServer.

Boots the real server binary on scratch ports with a throwaway SQLite database,
then exercises both wire surfaces the way a client would:

  * the HTTP API   (SiteMiddleware / ServerRouter)   — POST /scores, GET /leaderboard
  * the multiplayer referee (GameServer / MultiplayerCore) — line-JSON over TCP

It is self-contained (Python 3 stdlib only) so CI can run it right after building
the `server` preset:

    python3 Bootstrap/e2e.py ./build/FifteenServer

Pass --http-only to skip the multiplayer TCP race and run just the HTTP API
checks (used on Windows until a Winsock-hardened race script lands).

Exits non-zero if any check fails; the server is always torn down on the way out.
"""

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

# Scratch ports (kept out of the 8080/8091 dev defaults so a running dev server
# never collides with the test) and a throwaway database.
HOST = "127.0.0.1"
HTTP_PORT = 18080
MP_PORT = 18091
BOOT_TIMEOUT = 10.0  # seconds to wait for the server to start accepting
IO_TIMEOUT = 5.0  # per-request / per-message socket timeout

_failures = 0


def check(name, ok, detail=""):
    """Record and print a single PASS/FAIL line."""
    global _failures
    if ok:
        print("PASS  " + name)
    else:
        _failures += 1
        print("FAIL  " + name + (" -- " + detail if detail else ""))
    return ok


# --- server lifecycle --------------------------------------------------------


def wait_for_port(host, port, timeout):
    """Poll until (host, port) accepts a TCP connection, or the timeout lapses."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def shutdown(proc):
    """Stop the server (SIGTERM, then SIGKILL if it does not exit)."""
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def dump_log(path):
    """Print the tail of the captured server log (for boot failures)."""
    try:
        with open(path, "r", errors="replace") as handle:
            tail = handle.read()[-2000:]
    except OSError:
        return
    if tail.strip():
        print("--- server log (tail) ---")
        print(tail)
        print("--- end server log ---")


def cleanup_dir(path):
    """Remove the temp directory and everything in it (incl. sqlite side files)."""
    try:
        for name in os.listdir(path):
            try:
                os.remove(os.path.join(path, name))
            except OSError:
                pass
        os.rmdir(path)
    except OSError:
        pass


# --- HTTP API ----------------------------------------------------------------


def http_request(method, path, body=None):
    """Return (status, text). Non-2xx statuses come back instead of raising;
    a transport failure (server gone) comes back as (0, reason) so a check can
    fail cleanly rather than crash the script."""
    url = "http://{}:{}{}".format(HOST, HTTP_PORT, path)
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=IO_TIMEOUT) as response:
            return response.getcode(), response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8")
    except (urllib.error.URLError, OSError) as error:
        return 0, "transport error: {}".format(error)


def run_http_checks():
    # A valid submission is accepted (201 Created).
    status, _ = http_request(
        "POST",
        "/scores",
        {"name": "Ada", "gridSize": 4, "moves": 120, "duration": 63, "playedAt": 100.0},
    )
    check("POST /scores (valid) -> 201", status == 201, "got {}".format(status))

    # A malformed/invalid submission is rejected by server-side validation (400).
    status, _ = http_request(
        "POST",
        "/scores",
        {"name": "", "gridSize": 99, "moves": -1, "duration": -1, "playedAt": 0},
    )
    check("POST /scores (invalid) -> 400", status == 400, "got {}".format(status))

    # The submitted row shows up on the leaderboard for its board size.
    status, text = http_request("GET", "/leaderboard?size=4")
    try:
        rows = json.loads(text)
    except ValueError:
        rows = None
    has_ada = isinstance(rows, list) and any(
        isinstance(row, dict)
        and row.get("name") == "Ada"
        and row.get("gridSize") == 4
        and row.get("moves") == 120
        for row in rows
    )
    check(
        "GET /leaderboard?size=4 -> 200 with Ada row",
        status == 200 and has_ada,
        "status={} body={}".format(status, text[:200]),
    )

    # An unknown route is a 404 (not the 400 reserved for a bad /scores body).
    status, _ = http_request("GET", "/nope")
    check("GET /nope -> 404", status == 404, "got {}".format(status))


# --- multiplayer (line-JSON over TCP) ----------------------------------------


class LineSocket:
    """Newline-delimited JSON client over a blocking TCP socket."""

    def __init__(self, host, port, timeout=IO_TIMEOUT):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buffer = b""

    def send_json(self, obj):
        self.sock.sendall((json.dumps(obj) + "\n").encode("utf-8"))

    def recv_json(self):
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise EOFError("connection closed while awaiting a message")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line.decode("utf-8"))

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def run_mp_checks():
    client_a = None
    client_b = None
    try:
        # A joins first and must be queued. Waiting for `queued` also guarantees
        # the server has registered A before B joins, so the match is deterministic.
        client_a = LineSocket(HOST, MP_PORT)
        client_a.send_json({"type": "join", "name": "Ada", "gridSize": 4})
        queued = client_a.recv_json()
        check("MP: A join -> queued", queued.get("type") == "queued", "got {}".format(queued))

        # B joins the same board size, so the server matches the two and deals a board.
        client_b = LineSocket(HOST, MP_PORT)
        client_b.send_json({"type": "join", "name": "Bob", "gridSize": 4})

        start_b = client_b.recv_json()  # B's only message so far
        start_a = client_a.recv_json()  # A's second message, after `queued`
        both_start = start_a.get("type") == "start" and start_b.get("type") == "start"
        check("MP: both receive start", both_start, "A={} B={}".format(start_a, start_b))
        check(
            "MP: start seeds match",
            both_start and start_a.get("seed") == start_b.get("seed"),
            "A.seed={} B.seed={}".format(start_a.get("seed"), start_b.get("seed")),
        )
        check(
            "MP: opponentName correct",
            both_start
            and start_a.get("opponentName") == "Bob"
            and start_b.get("opponentName") == "Ada",
            "A.opponentName={} B.opponentName={}".format(
                start_a.get("opponentName"), start_b.get("opponentName")
            ),
        )

        # A leaves; B wins by walkover and is told the opponent left.
        client_a.send_json({"type": "leave"})
        left = client_b.recv_json()
        check(
            "MP: A leaves -> B gets opponentLeft",
            left.get("type") == "opponentLeft",
            "got {}".format(left),
        )
    except (OSError, EOFError, ValueError) as error:
        check("MP: race protocol", False, "exception: {}".format(error))
    finally:
        if client_a is not None:
            client_a.close()
        if client_b is not None:
            client_b.close()


# --- entry point -------------------------------------------------------------


def main():
    args = sys.argv[1:]
    http_only = "--http-only" in args
    positional = [arg for arg in args if not arg.startswith("-")]
    server_bin = positional[0] if positional else "./build/FifteenServer"
    if not os.path.exists(server_bin):
        print("FAIL  server binary not found: {}".format(server_bin))
        return 1

    tmpdir = tempfile.mkdtemp(prefix="fifteen-e2e-")
    db_path = os.path.join(tmpdir, "e2e.sqlite3")
    log_path = os.path.join(tmpdir, "server.log")

    env = dict(os.environ)
    env["FIFTEEN_SERVER_PORT"] = str(HTTP_PORT)
    env["FIFTEEN_SERVER_MP_PORT"] = str(MP_PORT)
    env["FIFTEEN_SERVER_DATABASE"] = db_path

    log = open(log_path, "wb")
    proc = subprocess.Popen([server_bin], env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        print("Booting {} (http :{}, mp :{}) ...".format(server_bin, HTTP_PORT, MP_PORT))
        if not wait_for_port(HOST, HTTP_PORT, BOOT_TIMEOUT):
            log.flush()
            print("FAIL  server did not accept HTTP on :{} within {:.0f}s".format(HTTP_PORT, BOOT_TIMEOUT))
            dump_log(log_path)
            return 1
        if proc.poll() is not None:
            print("FAIL  server exited early (code {})".format(proc.returncode))
            dump_log(log_path)
            return 1
        run_http_checks()

        if http_only:
            print("(--http-only: skipping the multiplayer TCP race)")
        else:
            # The multiplayer port binds alongside HTTP; give it the same grace period.
            if not wait_for_port(HOST, MP_PORT, BOOT_TIMEOUT):
                log.flush()
                print("FAIL  server did not accept TCP on :{} within {:.0f}s".format(MP_PORT, BOOT_TIMEOUT))
                dump_log(log_path)
                return 1
            run_mp_checks()
        # If anything failed, the server log usually explains it (a crash, a
        # bind failure, a mid-run exit).
        if _failures or proc.poll() is not None:
            log.flush()
            dump_log(log_path)
    finally:
        shutdown(proc)
        log.close()
        cleanup_dir(tmpdir)

    if _failures:
        print("\n{} check(s) FAILED".format(_failures))
        return 1
    print("\nAll checks PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
