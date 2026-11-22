# Using Cadence

## Chatbox

Expand a source to change what it shows. Use the arrows beside its name to move
it up or down. **Layout Options → Compact Layout** joins sources with the
separator you choose. The preview shows what will be sent; messages beyond
the displayed character or line limit are not sent.

**Pause Sending** keeps the preview running without sending it to VRChat.
**Send Once** sends the current preview while leaving automatic updates paused.

## Status

Choose **Add Status** to save a phrase such as “Be right back”. Use a
phrase’s switch to replace the entire chatbox with that exact text, even if
you use a saved template. Only one status can be active at a time; turning it
off restores your usual message. The Chatbox preview also has a **Turn Off**
button while a status is active.

The row menu lets you edit, reorder, or delete phrases. Phrases are saved
locally, but the active switch always starts off when you reopen Cadence.
**Pause Sending** still pauses automatic sends; use **Send Once** if you want
to send the active status without resuming automatic updates.

## Sources

| Source | Setup |
| --- | --- |
| Clock | Local time, with 12- or 24-hour format and optional date and seconds. |
| Music | Uses MPRIS, supported by many Linux players and browsers. Progress appears only when the player reports position and duration. |
| Lyrics | Opt in to LRCLIB lookup and preview one synced line at a time before sharing. |
| Heart Rate | Connect a Pulsoid access token with the `data:heart_rate:read` scope on the Connectivity tab. Heart Rate settings are on Chatbox. |
| Custom Text | Add a static line of your own. |
| Battery | Shows system battery level and, optionally, charging status. Omitted when no battery is detected. |
| System Stats | Opt-in CPU and memory usage from Linux. |
| Session Timer | A stopwatch or countdown with Start, Pause, and Reset. Enable **Include in Chatbox** to share it. |
| Weather | Search for a city, select the matching region, and turn on **Include in Chatbox**. Choose °C, °F, or alternating units and whether to show the icon and condition. |
| VRChat Status | Opt in to show your mic mute state, Earmuff Mode, either hand’s gesture, or movement speed. Each field is optional. |

Weather never detects where you are. Clicking **Search** sends only the text
you entered to [Open-Meteo](https://open-meteo.com/), which provides the weather
data. Selecting a city saves that preset locally. Forecasts use its
coordinates and refresh approximately every 15 minutes while Weather is
enabled. The city name isn’t sent to VRChat.

### Lyrics

Expand **Lyrics** below Music and turn on **Look Up Lyrics Online**. Lookup
sends the track’s title, artist, album (if available), and duration to
[LRCLIB](https://lrclib.net/), including while previewing. No audio is sent.

Check **Lyric Preview** before turning on **Include in Chatbox**. Lyrics use
your player’s MPRIS position; they stay out if playback is paused, position is
unavailable, or no synced match is found. You can adjust the timing offset
and change or hide the lyric icon (the default is `❝`). Long lines are
shortened or omitted if they would crowd out the rest of your chatbox.

### VRChat Status

Enable the source on **Chatbox**, then pick the fields you want to share.
**Only Show Active States** hides an unmuted mic, off earmuffs, neutral
gestures, and zero speed. You can set the speed units and field separator.

Cadence reads VRChat’s [read-only OSC status outputs](https://wiki.vrchat.com/wiki/OSC#Avatar)
on `127.0.0.1:9001` by default. It opens no listener while the source is off.
It never accesses microphone audio or changes game controls; values only go
to the chatbox if you enable that source and field. Local UDP packets are not
authenticated, so do not treat the display as a privacy or microphone control.

If the source is waiting for data, confirm OSC is enabled, then try changing a
gesture, moving, or toggling mute in VRChat. Cadence clears cached values on
avatar change or after a minute without supported updates. If another OSC app
already listens on `9001`, close it, or use a router or a separate VRChat output
port. Set the matching port under **Connectivity → Advanced OSC** and retry.

## Connectivity

Use **Connectivity → Send Test** to verify OSC delivery, or change how often
Cadence updates the chatbox. Chatbox sends from Cadence are silent, including
test messages.

Cadence sends to `127.0.0.1:9000` by default. VRChat Status, if enabled, listens
on `127.0.0.1:9001`. Only change these under **Advanced OSC** if you’ve changed
[VRChat’s OSC ports](https://docs.vrchat.com/docs/osc-overview) or use an OSC router.

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
