"""Verify real Sunshine Avahi browse-domain publication and shutdown cleanup."""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import dbus
import dbus.mainloop.glib
from gi.repository import GLib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sunshine", type=Path, required=True)
parser.add_argument("--runtime-library-path", required=True)
parser.add_argument("--interface", default="wlP2p33s0")
args = parser.parse_args()
root = Path(os.environ["STREAMHUB_ROOT"]).resolve()
state = Path(tempfile.mkdtemp(prefix="browse-domain-", dir=root / "var/tests"))
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
server = dbus.Interface(bus.get_object("org.freedesktop.Avahi", "/"), "org.freedesktop.Avahi.Server")
path = server.RecordBrowserNew(socket.if_nametoindex(args.interface), 0,
                              "lb._dns-sd._udp.local", 1, 12, 0)
browser = dbus.Interface(bus.get_object("org.freedesktop.Avahi", path),
                         "org.freedesktop.Avahi.RecordBrowser")
events = []
expected = bytes([5]) + b"local" + bytes([0])
def record(kind, interface, protocol, name, clazz, rtype, data, flags):
    if bytes(data) == expected:
        events.append(kind)
browser.connect_to_signal("ItemNew", lambda *values: record("add", *values))
browser.connect_to_signal("ItemRemove", lambda *values: record("remove", *values))
context = GLib.MainContext.default()
def pump(seconds, predicate=lambda: False):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        while context.pending():
            context.iteration(False)
        if predicate():
            return True
        time.sleep(0.02)
    return predicate()
child = None
try:
    pump(1)
    if "add" in events:
        raise RuntimeError("A browse-domain record already exists; run on an isolated host or stop its owner first")
    probes = []
    try:
        for kind, offsets in [(socket.SOCK_STREAM, [-5, 0, 1, 21]), (socket.SOCK_DGRAM, [9, 10, 11, 13])]:
            for offset in offsets:
                probe = socket.socket(socket.AF_INET, kind)
                probes.append(probe)
                probe.bind(("0.0.0.0", 50089 + offset))
    finally:
        for probe in probes:
            probe.close()
    lines = ["port = 50089", "sunshine_name = StreamHub browse test", "upnp = disabled",
             "min_log_level = 2", "streamhub_socket = " + str(state / "absent.sock")]
    for key, filename in [("file_state", "state.json"), ("file_apps", "apps.json"),
                          ("credentials_file", "web.json"), ("pkey", "key.pem"),
                          ("cert", "cert.pem"), ("log_path", "sunshine.log")]:
        lines.append(key + " = " + str(state / filename))
    config = state / "sunshine.conf"
    config.write_text(chr(10).join(lines) + chr(10))
    (state / "apps.json").write_text('{"apps": []}')
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = args.runtime_library_path
    env["DBUS_SYSTEM_BUS_ADDRESS"] = "unix:path=/run/dbus/system_bus_socket"
    with (state / "console.log").open("w") as output:
        child = subprocess.Popen([str(args.sunshine.resolve()), str(config)], env=env,
                                 stdout=output, stderr=output)
        assert pump(12, lambda: "add" in events), "Sunshine did not publish the local browse domain"
        assert child.poll() is None, "Sunshine exited before verification"
        child.terminate()
        assert child.wait(timeout=12) == 0, "Sunshine failed during shutdown"
        assert pump(10, lambda: "remove" in events), "Browse-domain record survived Sunshine shutdown"
    print("PASS: actual Avahi PTR local domain added and removed with Sunshine; logs=" + str(state))
finally:
    if child is not None and child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
    browser.Free()
