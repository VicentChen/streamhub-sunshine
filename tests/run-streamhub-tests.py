"""Run bounded StreamHub transport and retained regressions with isolated user state."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--regression", action="store_true")
parser.add_argument("--runtime-library-path", help="Optional LD_LIBRARY_PATH for the test child")
args = parser.parse_args()
build = args.build.resolve()
suites = ["StreamHub*Test.*"]
if args.regression:
    suites += ["ConcatAndInsertTests.*", "ControlPacketTests.*", "EntryHandlerTests.*",
               "ClientAuthorizationTest.*", "CryptoTest.*", "BindAddressTest.*",
               "*MdnsInstanceNameTest*", "*UrlEscapeTest*", "*UrlGetHostTest*",
               "ConfigConsistencyTest.*", "LocaleConsistencyTest.*",
               "GamepadProtocolTest.*", "OpusPcmTest.*", "ProcessPNGTest.*",
               "ConfigHttpTest.ApplicationMetadataOmitsExecutionSettings"]
with tempfile.TemporaryDirectory(prefix="sunshine-protocol-") as directory:
    env = os.environ.copy()
    if args.runtime_library_path:
        env["LD_LIBRARY_PATH"] = args.runtime_library_path
    for key in ("CONFIGURATION_DIRECTORY", "DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
        env.pop(key, None)
    env.update(HOME=directory, TMPDIR=directory, XDG_CONFIG_HOME=directory,
               GCOV_PREFIX=directory, GCOV_PREFIX_STRIP="0")
    result = subprocess.run(
        [str(build / "tests/test_sunshine"), "--gtest_filter=" + ":".join(suites),
         "--gtest_output=xml:" + str(build / "streamhub-transport.xml")],
        cwd=build / "tests", env=env, timeout=60 if args.regression else 30)
    raise SystemExit(result.returncode)
