# Install and update Cadence

The installer downloads the official x86-64 Linux AppImage and checks it
against the release’s `SHA256SUMS`. It runs without `sudo` and does not create
desktop launchers or command shortcuts.

```sh
curl -fL https://raw.githubusercontent.com/MORT1SXD/cadence-linux/main/install.sh -o install.sh
less install.sh
bash install.sh
```

Press `q` to exit `less`. You can install a specific release with
`bash install.sh --version v1.2.1`.

## Update

In newer AppImage builds, choose **Settings → Update This AppImage**. Cadence
shows the path it launched from and asks before replacing the file with the
latest published release. It verifies the checksum before the swap. Quit and
reopen Cadence afterward. No background updates run.

You can also quit Cadence and run `bash install.sh` again to update the managed
install, or point the script at a portable copy:

```sh
bash install.sh --replace "$HOME/Downloads/cadence-x86_64.AppImage"
```

Your settings carry over either way. Source builds open the release page instead
of replacing themselves.

Open the AppImage from `~/.local/share/cadence/appimage/` (or the equivalent
under `$XDG_DATA_HOME`). If FUSE isn’t available, run the AppImage with
`--appimage-extract-and-run`. Wayland desktops that support window icons can
show Cadence's taskbar icon without a launcher. Other desktops may show a
generic taskbar icon; this does not affect the tray icon.
