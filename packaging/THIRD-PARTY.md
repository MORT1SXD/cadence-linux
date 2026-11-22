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

Use `./cadence-x86_64.AppImage --appimage-extract` to inspect the image.
