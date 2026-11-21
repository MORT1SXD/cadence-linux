# Using Cadence

## Chatbox

Expand a source to change what it shows. Use the arrows beside its name to move
it up or down. **Layout Options → Compact Layout** joins sources with the
separator you choose. The preview shows what will be sent; messages beyond
the displayed character or line limit are not sent.

**Pause Sending** keeps the preview running without sending it to VRChat.
**Send Once** sends the current preview while leaving automatic updates paused.
Pausing VRChat output does not turn off Discord sharing.

| Source | Setup |
| --- | --- |
| Clock | Local time, with 12- or 24-hour format and optional date and seconds. |
| Music | Uses MPRIS, supported by many Linux players and browsers. Progress appears only when the player reports position and duration. |
| Heart Rate | Connect a Pulsoid access token with the `data:heart_rate:read` scope on the Connectivity tab. Heart Rate settings are on Chatbox. |
| Custom Text | Add a static line of your own. |
| Battery | Shows system battery level and, optionally, charging status. Omitted when no battery is detected. |
| System Stats | Opt-in CPU and memory usage from Linux. |
| Session Timer | A stopwatch or countdown with Start, Pause, and Reset. Enable **Include in Chatbox** to share it. |

## Connectivity

Cadence sends to `127.0.0.1:9000` by default. Under **Connectivity** you can
change the port, adjust the update interval, test delivery, or disable
notification sounds.

Discord can share your track and/or heart rate, but only after you enable it and
confirm the sharing prompt. The AppImage includes Discord support without a
bundled Application ID. If you want to use it, create an application in the
[Discord Developer Portal](https://discord.com/developers/applications), close
Cadence, and add `app_id=YOUR_ID` to the `[discord]` section of
`~/.config/cadence/state.ini`. Then reopen Cadence.

## Troubleshooting

- **The AppImage won’t start:** run it in a terminal. If it mentions FUSE, use
  `./cadence-x86_64.AppImage --appimage-extract-and-run`. The build targets
  x86-64 systems with Ubuntu 24.04–era glibc or newer. For older hosts, see
  [building from source](build.md).
- **The preview works but VRChat shows nothing:** confirm OSC is enabled, the
  port matches, and you are in a world. Try **Send Test**. The OSC indicator
  confirms Cadence sent a packet, not that the game received it.
- **A source is missing:** expand its settings and check **Include in
  Chatbox**. Sources without data are omitted. If **Use saved template** is
  on, the saved template takes precedence over source settings.
- **Pulsoid connects but no BPM appears:** make sure your monitor is streaming
  and the token has the `data:heart_rate:read` scope.

Your settings live in `~/.config/cadence/state.ini`. It may contain your
Pulsoid access token; don’t attach it to an issue or share it unredacted.
