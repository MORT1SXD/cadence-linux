# Install and update Cadence

The installer downloads the official x86-64 Linux AppImage and checks it
against the release’s `SHA256SUMS`. It installs for your user, without `sudo`,
and adds **Cadence** to your desktop’s application list.

```sh
curl -fL https://raw.githubusercontent.com/MORT1SXD/cadence-linux/main/install.sh -o install.sh
less install.sh
bash install.sh
```

Press `q` to exit `less`. If FUSE isn’t available, run `bash install.sh --extract`
instead. You can also install a specific release with
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

The AppImage is installed under `~/.local/share/cadence/appimage/` (or the
equivalent under `$XDG_DATA_HOME`). The installer adds `~/.local/bin/cadence`
unless another command already uses that path. If the directory is on your
`PATH`, run `cadence` from a terminal. For non-FUSE terminal use, run
`cadence --appimage-extract-and-run`.
