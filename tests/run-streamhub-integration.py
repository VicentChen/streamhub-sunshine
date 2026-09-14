"""Run an isolated, finite Provider/Sunshine instance for Moonlight acceptance.

No existing services, configuration, or pairing database are modified. Runtime
files/logs stay in --state for inspection. SIGINT/SIGTERM and the time limit stop
both child processes. Use --source hdmi only with the explicitly selected device.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import signal
import socket
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--provider", type=Path, required=True)
parser.add_argument("--state", type=Path, required=True)
parser.add_argument("--runtime-library-path", help="Optional LD_LIBRARY_PATH for the Sunshine child only")
parser.add_argument("--reuse-state", action="store_true", help="Reuse only a directory created by this test runner")
parser.add_argument("--source", choices=["hdmi", "test-cycle"], default="test-cycle")
parser.add_argument("--device", default="/dev/video0")
parser.add_argument("--port", type=int, default=49089)
parser.add_argument("--seconds", type=int, default=120)
args = parser.parse_args()
if not 1 <= args.seconds <= 1800 or not 1024 <= args.port <= 65514:
    parser.error("seconds must be 1..1800 and port 1024..65514")
state = args.state.resolve()
if args.reuse_state:
    if not (state / "integration-state").is_file():
        parser.error("--reuse-state requires this runner's marker")
else:
    state.mkdir(mode=0o700, parents=True, exist_ok=False)
    (state / "integration-state").write_text("StreamHub finite integration state v1\n")
# Fail without disturbing a listener on any required endpoint.
probes = []
try:
    for kind, offsets in [(socket.SOCK_STREAM, [-5, 0, 1, 21]), (socket.SOCK_DGRAM, [9, 10, 11, 13])]:
        for offset in offsets:
            probe = socket.socket(socket.AF_INET, kind)
            probes.append(probe)
            if kind == socket.SOCK_STREAM:
                probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(("0.0.0.0", args.port + offset))
finally:
    for probe in probes:
        probe.close()
env = os.environ.copy()
for key in ("CONFIGURATION_DIRECTORY", "DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
    env.pop(key, None)
env.update(HOME=str(state), XDG_CONFIG_HOME=str(state), TMPDIR=str(state), GCOV_PREFIX=str(state), GCOV_PREFIX_STRIP="0")
# Runtime library paths may be supplied by the caller; do not impose a toolchain.
if args.runtime_library_path:
    env["LD_LIBRARY_PATH"] = args.runtime_library_path
sunshine = args.build.resolve() / "sunshine"
provider = args.provider.resolve()
sock = str(state / "provider.sock")
(state / "apps.json").write_text('{"apps":[]}')
configuration = state / "sunshine.conf"
configuration.write_text("\n".join([
    f"streamhub_socket = {sock}", "streamhub_codecs = h264,hevc",
    f"port = {args.port}", "sunshine_name = StreamHub acceptance",
    f"file_apps = {state / 'apps.json'}", f"file_state = {state / 'sunshine-state.json'}",
    f"credentials_file = {state / 'web-credentials.json'}",
    f"log_path = {state / 'sunshine.log'}", "min_log_level = 1", "upnp = disabled",
]) + "\n")
password = json.loads((state / "web-auth.json").read_text())["password"] if args.reuse_state else secrets.token_urlsafe(24)
(state / "web-auth.json").write_text(json.dumps({"username": "acceptance", "password": password, "port": args.port + 1}))
os.chmod(state / "web-auth.json", 0o600)
children = []
stopping = False
provider_stopped = False
def stop(signum, frame):
    global stopping
    stopping = True
signal.signal(signal.SIGINT, stop)
signal.signal(signal.SIGTERM, stop)
try:
    with (state / "credentials.log").open("w") as log:
        subprocess.run([str(sunshine), str(configuration), "--creds", "acceptance", password], env=env, stdout=log, stderr=log, check=True, timeout=10)
    with (state / "provider.log").open("a" if args.reuse_state else "w") as log, (state / "sunshine-console.log").open("a" if args.reuse_state else "w") as output:
        # The Provider is independent and uses its own system runtime.
        provider_env = env.copy()
        provider_env.pop("LD_LIBRARY_PATH", None)
        children.append(subprocess.Popen([str(provider), "serve", "--socket", sock, "--source", args.source, "--device", args.device], env=provider_env, stdout=log, stderr=log))
        children.append(subprocess.Popen([str(sunshine), str(configuration)], env=env, stdout=output, stderr=output))
        print(f"state={state} Moonlight port={args.port} source={args.source} duration={args.seconds}s", flush=True)
        deadline = time.monotonic() + args.seconds
        while not stopping and time.monotonic() < deadline:
            command_file = state / "provider-command"
            if command_file.exists():
                command = command_file.read_text().strip()
                command_file.unlink()
                if command == "stop" and children[0].poll() is None:
                    children[0].terminate(); children[0].wait(timeout=10)
                    provider_stopped = True
                elif command == "start" and children[0].poll() is not None:
                    provider_stopped = False
                    children[0] = subprocess.Popen([str(provider), "serve", "--socket", sock, "--source", args.source, "--device", args.device], env=provider_env, stdout=log, stderr=log)
                else:
                    raise RuntimeError("invalid Provider command or state")
            (state / "pids.json").write_text(json.dumps({"provider": children[0].pid, "sunshine": children[1].pid}))
            if not provider_stopped and children[0].poll() is not None:
                raise RuntimeError("Provider exited unexpectedly")
            for child in children[1:]:
                if child.poll() is not None:
                    raise RuntimeError(f"child exited early: pid={child.pid} exit={child.returncode}")
            time.sleep(.2)
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
    for child in reversed(children):
        try:
            child.wait(timeout=12)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
            print(f"ERROR: forced child cleanup pid={child.pid}", flush=True)
