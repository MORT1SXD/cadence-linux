import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile


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
    app = subprocess.Popen([str(Path(sys.argv[1]).resolve())], env=env,
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
    finally:
        if app.poll() is None:
            os.killpg(app.pid, signal.SIGTERM)
            try:
                app.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(app.pid, signal.SIGKILL)
                app.wait()
