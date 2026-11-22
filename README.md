# <img src="src/cadence.png" alt="" width="48" height="48"> Cadence

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

The AppImage includes GTK, libadwaita, and libwebsockets. If FUSE isn’t available,
run it with `--appimage-extract-and-run`. The [install script](docs/install.md)
can download or update the AppImage without creating shortcuts.

## Set up VRChat

1. In VRChat, enable **Action Menu → Options → OSC → Enabled**.
2. Enter a world. In Cadence, choose **Connectivity → Send Test**. If the
   test appears in your chatbox, you’re set.
3. Open **Chatbox** in Cadence and enable the sources you want to share.

Leave the ports alone unless you’ve configured custom ports or an OSC router.
For help, see the [user guide](docs/README.md#troubleshooting).

## Make it yours

**Chatbox** has a live preview and expandable source settings. Reorder sources,
give each one its own line, or put everything on one line. Pause automatic
updates while you edit, then **Send Once** to try the preview in-game.
Use **Status** to replace your chatbox with a saved phrase until you turn it off.

Music comes from MPRIS, with optional synced lyrics. Heart rate comes from
Pulsoid. Clock, session timer, battery, CPU/RAM, and custom text work offline.
Weather is optional and uses a city you choose, never location detection.
VRChat Status can show mic mute, gestures, and movement using local OSC.

[Build from source](docs/build.md) · [User guide](docs/README.md) · [MIT License](LICENSE)

> [!WARNING]
> Parts of the code and documentation were written with AI assistance and reviewed by the maintainer.
