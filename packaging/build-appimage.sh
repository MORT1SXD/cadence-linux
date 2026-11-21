#!/usr/bin/env bash
# Intended for an Ubuntu 24.04 x86-64 build host.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(realpath -m "${1:-$root/build/appimage}")
output=$(realpath -m "${2:-$root/build/dist}")
mkdir -p "$work" "$output"
appdir=$(mktemp -d "$work/AppDir.XXXXXX")
trap 'rm -rf -- "$appdir"' EXIT
tools="$work/tools"
mkdir -p "$tools"

download() {
    local file=$1 digest=$2 url=$3
    if ! (cd "$tools" && sha256sum --check --status <<< "$digest  $file"); then
        curl --fail --location --retry 3 --output "$tools/$file" "$url"
        (cd "$tools" && sha256sum --check <<< "$digest  $file")
    fi
    chmod +x "$tools/$file"
}

download linuxdeploy c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
download appimagetool e8c1169c4af66c3184d7ee110713d4be04befe671fa358e2c1591ee840553b03 \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
download runtime-x86_64 617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46 \
    https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64

cmake -S "$root" -B "$work/cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$work/cmake" --parallel
DESTDIR="$appdir" cmake --install "$work/cmake" --strip

pixbuf_dir=/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0
svg="$pixbuf_dir/2.10.0/loaders/libpixbufloader-svg.so"
install -d "$appdir/usr/share/glib-2.0/schemas" "$appdir/usr/share/icons/hicolor"
while IFS= read -r schema; do
    case "$schema" in
        /usr/share/glib-2.0/schemas/*.xml) cp "$schema" "$appdir/usr/share/glib-2.0/schemas/" ;;
    esac
done < <(dpkg -L gsettings-desktop-schemas libgtk-4-common)
glib-compile-schemas --strict "$appdir/usr/share/glib-2.0/schemas"
cp -a /usr/share/icons/Adwaita "$appdir/usr/share/icons/"
cp /usr/share/icons/hicolor/index.theme "$appdir/usr/share/icons/hicolor/"

export APPIMAGE_EXTRACT_AND_RUN=1
"$tools/linuxdeploy" --appdir "$appdir" \
    --desktop-file "$root/packaging/dev.cadence.app.desktop" \
    --icon-file "$root/packaging/dev.cadence.app.svg" \
    --executable "$pixbuf_dir/gdk-pixbuf-query-loaders" \
    --library "$svg" --custom-apprun "$root/packaging/AppRun"

install -d "$appdir/usr/lib/cadence-loaders"
mv "$appdir/usr/lib/libpixbufloader-svg.so" "$appdir/usr/lib/cadence-loaders/"
for lib in libgtk-4.so.1 libadwaita-1.so.0; do
    test -e "$appdir/usr/lib/$lib"
done
compgen -G "$appdir/usr/lib/libwebsockets.so.*" >/dev/null

# Record Ubuntu source package versions and ship the copyright notices for
# libraries and data copied into the image.
notices="$appdir/usr/share/doc/cadence/third-party"
mkdir -p "$notices"
{
    printf '%s\n' adwaita-icon-theme gsettings-desktop-schemas libgtk-4-common hicolor-icon-theme
    while IFS= read -r -d '' file; do
        base=$(basename -- "$file")
        dpkg-query -S "*/$base" 2>/dev/null | sed 's/: .*//' || true
    done < <(find "$appdir/usr/lib" "$appdir/usr/bin" -type f -print0)
} | sort -u | while IFS= read -r package; do
    name=${package%%:*}
    if [[ -f "/usr/share/doc/$name/copyright" ]]; then
        cp -L "/usr/share/doc/$name/copyright" "$notices/$name"
        dpkg-query -W -f='${binary:Package}\t${source:Package}\t${source:Version}\n' "$package"
    fi
done > "$notices/ubuntu-sources.tsv"
cp "$root/packaging/THIRD-PARTY.md" "$notices/README.md"
cp "$work/cmake/_deps/discord_rpc-src/LICENSE" "$notices/discord-rpc"
find "$work/cmake/_deps/discord_rpc-src/thirdparty" -iname license -path '*rapidjson*' \
    -exec cp '{}' "$notices/rapidjson" \;

ARCH=x86_64 "$tools/appimagetool" --runtime-file "$tools/runtime-x86_64" \
    "$appdir" "$output/cadence-x86_64.AppImage"
(cd "$output" && sha256sum cadence-x86_64.AppImage > SHA256SUMS)
