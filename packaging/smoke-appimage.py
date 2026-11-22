import argparse
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import tempfile
import time


parser = argparse.ArgumentParser()
parser.add_argument("appimage")
parser.add_argument("--check-wayland-icon", action="store_true",
                    help="also check the running window sends icon pixels to a compatible Wayland compositor")
args = parser.parse_args()


def check_window_icon(log, app):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline and app.poll() is None:
        trace = log.read_text(errors="replace")
        for window in re.findall(r'-> (xdg_toplevel#\d+)\.set_app_id\("dev\.cadence\.app"\)', trace):
            for icon in re.findall(rf"\.set_icon\({window}, (xdg_toplevel_icon_v1#\d+)\)", trace):
                if f"-> {icon}.add_buffer(" in trace:
                    print("Cadence sent window icon pixels to the Wayland compositor.")
                    return
        time.sleep(0.05)
    raise AssertionError("Cadence did not send window icon pixels; the compositor must support xdg-toplevel-icon.")


with tempfile.TemporaryDirectory() as temp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
    listener.bind(("127.0.0.1", 0))
    listener.settimeout(20)
    config = Path(temp, "cadence")
    config.mkdir()
    config.joinpath("state.ini").write_text(
        f"[osc]\nport={listener.getsockname()[1]}\n[song]\nenabled=false\n"
        "[output]\nnotification_sfx=true\n"
        "[display]\nbox_drawing=true\n"
        "[status]\nphrases=Be right back;\n",
        encoding="utf-8",
    )
    env = dict(os.environ, XDG_CONFIG_HOME=temp, GTK_A11Y="none",
               G_DEBUG="fatal-criticals", GSK_RENDERER="cairo",
               APPIMAGE_EXTRACT_AND_RUN="1")
    log = Path(temp, "wayland.log")
    if args.check_wayland_icon:
        env.update(GDK_BACKEND="wayland", WAYLAND_DEBUG="client")
        with log.open("w") as stderr:
            app = subprocess.Popen([str(Path(args.appimage).resolve())], env=env,
                                   start_new_session=True, stderr=stderr)
    else:
        app = subprocess.Popen([str(Path(args.appimage).resolve())], env=env,
                               start_new_session=True)
    try:
        data, _ = listener.recvfrom(4096)
        fields = [item.decode("utf-8") for item in data.split(b"\0") if item]
        # Even an older config with notifications on must send silently.
        assert len(fields) == 3 and fields[:2] == ["/chatbox/input", ",sTF"], fields
        # Saved statuses stay off at startup, and legacy box drawing is ignored.
        assert re.fullmatch(r"⌚ \d{2}:\d{2}", fields[2]), fields
        assert app.poll() is None, "The app exited after sending"
        print("Received the clock from the packaged app:", fields[2])
        if args.check_wayland_icon:
            check_window_icon(log, app)
            # Let the compositor process the request before ending the test.
            listener.settimeout(5)
            listener.recvfrom(4096)
            assert app.poll() is None, "The app exited after sending the Wayland icon"
    finally:
        if app.poll() is None:
            os.killpg(app.pid, signal.SIGTERM)
            try:
                app.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(app.pid, signal.SIGKILL)
                app.wait()
