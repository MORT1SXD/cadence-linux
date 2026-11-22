#!/usr/bin/env bash
# Intended for an Ubuntu 24.04 x86-64 build host.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(realpath -m "${1:-$root/build/appimage}")
output=$(realpath -m "${2:-$root/build/dist}")
mkdir -p "$work" "$output"
appdir=$(mktemp -d "$work/AppDir.XXXXXX")
chmod 0755 "$appdir"
trap 'rm -rf -- "$appdir"' EXIT
tools="$work/tools"
mkdir -p "$tools"

download() {
    local file=$1 digest=$2 url=$3
    if ! (cd "$tools" && sha256sum --check --status <<< "$digest  $file"); then
        curl --fail --location --retry 3 --output "$tools/$file" "$url"
        (cd "$tools" && sha256sum --check <<< "$digest  $file")
    fi
}

download linuxdeploy c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
download appimagetool e8c1169c4af66c3184d7ee110713d4be04befe671fa358e2c1591ee840553b03 \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
download runtime-x86_64 617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46 \
    https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
chmod +x "$tools/linuxdeploy" "$tools/appimagetool" "$tools/runtime-x86_64"

# Noble's GTK predates xdg-toplevel-icon. Rebuild the installed Ubuntu source
# version with just that backport rather than raising the glibc baseline.
download xdg-toplevel-icon-v1.xml 791dbfed89280c1b8031689955a03e9b2da55601be3c5854a6198e38cea4978f \
    https://raw.githubusercontent.com/wayland-mirror/wayland-protocols/1.38/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml
gtk_version=$(dpkg-query -W -f='${source:Version}' libgtk-4-dev)
gtk_patch="$root/packaging/gtk-wayland-window-icon.patch"
patch_digest=$(sha256sum < "$gtk_patch")
gtk_work="$work/gtk-$gtk_version-${patch_digest:0:16}"
mkdir -p "$gtk_work"
if [[ ! -d "$gtk_work/source" ]]; then
    unpack=$(mktemp -d "$gtk_work/unpack.XXXXXX")
    (cd "$unpack" && apt-get source --only-source "gtk4=$gtk_version")
    gtk_sources=("$unpack"/gtk4-*/)
    test "${#gtk_sources[@]}" -eq 1 && test -f "${gtk_sources[0]}/meson.build"
    patch --batch --forward -d "${gtk_sources[0]}" -p1 < "$gtk_patch"
    cp "$tools/xdg-toplevel-icon-v1.xml" "${gtk_sources[0]}/gdk/wayland/protocol/"
    mv -T "${gtk_sources[0]%/}" "$gtk_work/source"
fi
if [[ ! -f "$gtk_work/build/build.ninja" ]]; then
    meson setup "$gtk_work/build" "$gtk_work/source" --buildtype=release --wrap-mode=nofallback \
        -Dintrospection=disabled -Dmedia-gstreamer=disabled -Dbuild-demos=false \
        -Dbuild-tests=false -Dbuild-testsuite=false -Dbuild-examples=false
fi
meson compile -C "$gtk_work/build" gtk-4
gtk_library=$(realpath -e "$gtk_work/build/gtk/libgtk-4.so.1")

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
LD_LIBRARY_PATH="${gtk_library%/*}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$tools/linuxdeploy" --appdir "$appdir" \
    --desktop-file "$root/packaging/dev.cadence.app.desktop" \
    --icon-file "$work/cmake/dev.cadence.app.png" \
    --executable "$pixbuf_dir/gdk-pixbuf-query-loaders" \
    --library "$svg" --custom-apprun "$root/packaging/AppRun"

install -d "$appdir/usr/lib/cadence-loaders"
mv "$appdir/usr/lib/libpixbufloader-svg.so" "$appdir/usr/lib/cadence-loaders/"
for lib in libgtk-4.so.1 libadwaita-1.so.0; do
    test -e "$appdir/usr/lib/$lib"
done
compgen -G "$appdir/usr/lib/libwebsockets.so.*" >/dev/null
# Keep the PNG as the AppImage's file icon; this does not install a launcher.
rm -f -- "$appdir/dev.cadence.app.png"
install -m 0644 "$work/cmake/dev.cadence.app.png" "$appdir/dev.cadence.app.png"
ln -sfn dev.cadence.app.png "$appdir/.DirIcon"
grep -aqF xdg_toplevel_icon_manager_v1 "$appdir/usr/lib/libgtk-4.so.1"

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
cp "$gtk_work/source/COPYING" "$notices/gtk-LGPL-2.1.txt"
cp "$gtk_patch" "$tools/xdg-toplevel-icon-v1.xml" "$notices/"
printf 'gtk4\t%s\n' "$gtk_version" > "$notices/gtk-backport-source.tsv"

ARCH=x86_64 "$tools/appimagetool" --runtime-file "$tools/runtime-x86_64" \
    "$appdir" "$output/cadence-x86_64.AppImage"
(cd "$output" && sha256sum cadence-x86_64.AppImage > SHA256SUMS)
