# Cadence

Put your music, heart rate, or a little note in your VRChat chatbox. Cadence is a
Linux desktop app that sends text through VRChat’s built-in OSC input.

```text
♪ Nujabes — Feather · 01:42 / 03:18
♥ 82 bpm
⌚ 21:30
```

## Get started

1. Download `cadence-x86_64.AppImage` from [Releases](https://github.com/MORT1SXD/cadence-linux/releases/latest).
2. Make it executable and open it:
   ```sh
   chmod +x cadence-x86_64.AppImage
   ./cadence-x86_64.AppImage
   ```
3. In VRChat, enable **Action Menu → Options → OSC → Enabled**, then enter a
   world. In Cadence, open **Connectivity → Send Test** to check it.

The AppImage includes GTK, libadwaita, and libwebsockets. If FUSE isn’t available,
run it with `--appimage-extract-and-run`. See the [user guide](docs/README.md)
for setup and troubleshooting.

## Make it yours

**Chatbox** has a live preview and expandable source settings. Reorder sources,
give each one its own line, or put everything on one line. Pause automatic
updates while you edit, then **Send Once** to try the preview in-game.

Music comes from MPRIS and heart rate from Pulsoid. Clock, session timer,
battery, CPU/RAM, and custom text all work without an account. Discord sharing
is optional and off by default.

[Build from source](docs/build.md) · [User guide](docs/README.md) · [MIT License](LICENSE)

> [!WARNING]
> Parts of the code and documentation were written with AI assistance and reviewed by the maintainer.
