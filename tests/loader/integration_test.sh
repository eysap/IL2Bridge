#!/usr/bin/env bash
# End-to-end LD_PRELOAD test for watcher discovery, IL2CPP resolution, IPC,
# and hook installation/removal.
set -euo pipefail

LOADER_SO="$1"
DUMMY_TARGET="$2"
BROKER="$3"

# Use Python's standard library to avoid an extra socket-client dependency.
if ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: python3 not found, cannot drive the IPC socket for this test"
    exit 1
fi

ipc_request() {
    # $1 = socket path, $2 = request line (no trailing newline)
    python3 - "$1" "$2" <<'PYEOF'
import socket
import sys

path = sys.argv[1]
request = sys.argv[2]

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(2)
s.connect(path)
s.sendall((request + "\n").encode())
data = s.recv(4096)
s.close()
sys.stdout.write(data.decode(errors="replace"))
PYEOF
}

RUNTIME_ROOT=$(mktemp -d)
chmod 700 "$RUNTIME_ROOT"
XDG_RUNTIME_DIR="$RUNTIME_ROOT" IL2BRIDGE_WATCHER_GRACE_MS=100 LD_PRELOAD="$LOADER_SO" "$DUMMY_TARGET" &
TARGET_PID=$!

# SIGTERM may skip shared-library destructors, so the test also removes its
# known socket path on exit.
SOCKET_PATH=""
trap 'if [ -n "${TARGET_PID:-}" ]; then kill "$TARGET_PID" 2>/dev/null || true; fi; rm -f "${SOCKET_PATH:-}" 2>/dev/null || true; rmdir "${RUNTIME_ROOT:-}/il2bridge" "${RUNTIME_ROOT:-}" 2>/dev/null || true' EXIT

# Give the delayed module load and watcher time to run before polling.
sleep 0.5

SOCKET_PATH="$RUNTIME_ROOT/il2bridge/${TARGET_PID}.sock"

# Poll within a fixed budget to tolerate scheduler variance.
FOUND=0
for _ in $(seq 1 20); do
    if [ -S "$SOCKET_PATH" ]; then
        FOUND=1
        break
    fi
    sleep 0.1
done

if [ "$FOUND" -ne 1 ]; then
    echo "FAIL: socket $SOCKET_PATH was not created within the wait budget"
    if ! kill -0 "$TARGET_PID" 2>/dev/null; then
        echo "       (dummy_target process is no longer running -- it likely exited or crashed)"
    fi
    exit 1
fi

RESPONSE=$(ipc_request "$SOCKET_PATH" "PING" || true)
if [ "$RESPONSE" != "OK PONG" ]; then
    echo "FAIL: expected 'OK PONG', got '$RESPONSE'"
    exit 1
fi
echo "PASS: loader responded to PING over IPC while injected into dummy_target"

# Exercise the complete resolve -> hook -> unhook chain.
IMAGE_RESP=$(ipc_request "$SOCKET_PATH" "RESOLVE-IMAGE Fake.dll" || true)
if [[ "$IMAGE_RESP" != OK\ * ]]; then
    echo "FAIL: RESOLVE-IMAGE expected 'OK <handle>', got '$IMAGE_RESP'"
    exit 1
fi
IMAGE_HANDLE=$(echo "$IMAGE_RESP" | awk '{print $2}')

CLASS_RESP=$(ipc_request "$SOCKET_PATH" "RESOLVE-CLASS $IMAGE_HANDLE FakeNamespace FakeClass" || true)
if [[ "$CLASS_RESP" != OK\ * ]]; then
    echo "FAIL: RESOLVE-CLASS expected 'OK <handle>', got '$CLASS_RESP'"
    exit 1
fi
CLASS_HANDLE=$(echo "$CLASS_RESP" | awk '{print $2}')

METHOD_RESP=$(ipc_request "$SOCKET_PATH" "RESOLVE-METHOD $CLASS_HANDLE FakeMethod 0" || true)
if [[ "$METHOD_RESP" != OK\ * ]]; then
    echo "FAIL: RESOLVE-METHOD expected 'OK <handle>', got '$METHOD_RESP'"
    exit 1
fi
METHOD_HANDLE=$(echo "$METHOD_RESP" | awk '{print $2}')

TOKEN_METHOD_RESP=$(ipc_request "$SOCKET_PATH" "RESOLVE-METHOD-TOKEN $CLASS_HANDLE 0x06000001" || true)
if [ "$TOKEN_METHOD_RESP" != "$METHOD_RESP" ]; then
    echo "FAIL: token resolution did not return the same method handle"
    exit 1
fi

HOOK_RESP=$(ipc_request "$SOCKET_PATH" "HOOK $METHOD_HANDLE replace trampoline skip-return-void" || true)
if [[ "$HOOK_RESP" != OK\ * ]]; then
    echo "FAIL: HOOK expected 'OK <handle>', got '$HOOK_RESP'"
    exit 1
fi
HOOK_HANDLE=$(echo "$HOOK_RESP" | awk '{print $2}')
echo "PASS: resolved FakeNamespace.FakeClass.FakeMethod and installed a trampoline hook on it (handle $HOOK_HANDLE)"

UNHOOK_RESP=$(ipc_request "$SOCKET_PATH" "UNHOOK $HOOK_HANDLE" || true)
if [ "$UNHOOK_RESP" != "OK" ]; then
    echo "FAIL: UNHOOK expected 'OK', got '$UNHOOK_RESP'"
    exit 1
fi
echo "PASS: uninstalled the hook cleanly"

# Drive the same lifecycle through the real broker client. No raw image,
# class, method, or hook slot is exposed to this shell.
STATUS_JSON=$("$BROKER" --json --socket "$SOCKET_PATH" status)
python3 - "$STATUS_JSON" <<'PYEOF'
import json, sys
data = json.loads(sys.argv[1])
assert data["ok"] is True
assert data["protocol"] == 1
assert data["activeHooks"] == 0
PYEOF

ADD_JSON=$("$BROKER" --json --socket "$SOCKET_PATH" hook add \
    'Fake.dll!FakeNamespace.FakeClass::FakeMethod/0' \
    --mode around --handler count-calls)
HOOK_ID=$(python3 - "$ADD_JSON" <<'PYEOF'
import json, sys
data = json.loads(sys.argv[1])
assert data["ok"] is True
print(data["id"])
PYEOF
)

LIST_JSON=$("$BROKER" --json --socket "$SOCKET_PATH" hook list)
python3 - "$LIST_JSON" "$HOOK_ID" <<'PYEOF'
import json, sys
data = json.loads(sys.argv[1])
assert data["ok"] is True
assert len(data["hooks"]) == 1
assert data["hooks"][0]["id"] == sys.argv[2]
assert data["hooks"][0]["method"] == "Fake.dll!FakeNamespace.FakeClass::FakeMethod@0x06000001"
PYEOF

STATS_JSON=$("$BROKER" --json --socket "$SOCKET_PATH" hook stats "$HOOK_ID")
python3 - "$STATS_JSON" <<'PYEOF'
import json, sys
data = json.loads(sys.argv[1])
assert data == {"hits": 0, "id": data["id"], "ok": True}
PYEOF

"$BROKER" --json --socket "$SOCKET_PATH" hook remove "$HOOK_ID" >/dev/null

EVENTS_JSON=$($BROKER --json --socket "$SOCKET_PATH" events read --after 0 --limit 16)
python3 - "$EVENTS_JSON" <<'PYEOF'
import json, sys
events = json.loads(sys.argv[1])["events"]
assert [event["type"] for event in events][-2:] == ["hook-added", "hook-removed"]
assert all(event["hookId"] for event in events)
PYEOF
echo "PASS: broker status and hook add/list/stats/remove lifecycle verified"

echo "PASS: full resolve -> hook -> unhook chain verified end-to-end under real LD_PRELOAD injection"

# Let the target exit normally so shared-library destructors run. The loader
# must join its watcher and remove the IPC path before process teardown.
wait "$TARGET_PID"
TARGET_PID=""
if [ -e "$SOCKET_PATH" ]; then
    echo "FAIL: socket $SOCKET_PATH remained after clean target shutdown"
    exit 1
fi
echo "PASS: clean shutdown joined the watcher and removed the IPC socket"
