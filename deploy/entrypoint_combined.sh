#!/bin/sh
# Combined-image entrypoint (P8): start the C++ gRPC engine in the background, wait
# until it's accepting connections, then exec the gateway in the foreground so Cloud
# Run tracks the gateway as the container's main process.
set -e

joho_server --corpus "$JOHO_CORPUS" --port 50051 &
ENGINE_PID=$!

# Wait (up to ~30s) for the engine to bind :50051 before starting the gateway, so the
# first request doesn't race the engine's index load.
python - <<'PY'
import socket, time, sys
for _ in range(100):
    try:
        with socket.create_connection(("localhost", 50051), timeout=0.5):
            print("engine is up", flush=True); sys.exit(0)
    except OSError:
        time.sleep(0.3)
print("warning: engine not reachable after 30s; starting gateway anyway", flush=True)
PY

# If the engine dies, take the container down with it.
trap 'kill $ENGINE_PID 2>/dev/null' INT TERM
exec uvicorn app:app --host 0.0.0.0 --port "${PORT:-8080}"
