# Bundled software

Cadence is MIT-licensed. The AppImage includes separately licensed Ubuntu
libraries and desktop data.
The image contains their notices under `usr/share/doc/cadence/third-party/`.

`ubuntu-sources.tsv` lists the binary package, corresponding Ubuntu source
package, and source version. To fetch a listed source, use Ubuntu 24.04 with
source repositories enabled and run `apt source PACKAGE=VERSION`. Ubuntu's
[source package archive](https://launchpad.net/ubuntu/+source) also lists
published versions. The [AppImage runtime](https://github.com/AppImage/type2-runtime)
has its own upstream source.

The bundled GTK is built from the Ubuntu source version in
`gtk-backport-source.tsv`, with a Wayland window-icon backport based on
[GTK’s upstream implementation](https://github.com/GNOME/gtk/commit/0c5599d1ca98f1d2ea861f4b9b4a60848d195610).
The image includes the patch, protocol XML, and GTK license. The build flags
and source-fetching steps are in `packaging/build-appimage.sh`.

Use `./cadence-x86_64.AppImage --appimage-extract` to inspect the image.
