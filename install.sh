#!/usr/bin/env bash
# Install or update the official AppImage for the current user.
set -euo pipefail

usage() {
    printf '%s\n' 'Usage: bash install.sh [--version vX.Y.Z] [--extract | --replace PATH]' \
        'Installs the latest release by default. Run again to update.' \
        'Use --extract if your desktop does not have FUSE.' \
        'Use --replace to update an existing AppImage in place.'
}
fail() { printf 'Cadence: %s\n' "$*" >&2; exit 1; }

version=latest
extract=false
replace=
while (($#)); do
    case $1 in
        --help|-h) usage; exit 0 ;;
        --version)
            (($# >= 2)) || fail 'Missing version after --version.'
            version=$2
            shift ;;
        --extract) extract=true ;;
        --replace)
            (($# >= 2)) && [[ -n $2 ]] || fail 'Missing AppImage path after --replace.'
            replace=$2
            shift ;;
        *) fail "Unknown option: $1" ;;
    esac
    shift
done
[[ $version == latest || $version =~ ^v[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9]+)*$ ]] ||
    fail 'Use a release version such as v1.2.1.'
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] ||
    fail 'The current AppImage is for x86-64 Linux.'
((EUID != 0)) || fail 'Run the installer as your regular user, without sudo.'
command -v curl >/dev/null && command -v sha256sum >/dev/null ||
    fail 'Install curl and sha256sum (coreutils) first.'

asset=cadence-x86_64.AppImage
if [[ -n $replace ]]; then
    "$extract" && fail '--extract applies only to new desktop installations.'
    appimage=$(realpath -e -- "$replace") || fail 'Could not find that AppImage.'
    install_dir=${appimage%/*}
    install_dir=${install_dir:-/}
    [[ -f $appimage && -O $appimage && -x $appimage && -w $appimage && -w $install_dir ]] ||
        fail 'This AppImage isn’t writable by your user. Download the update manually.'
    # Accept type 2 AppImages, not an arbitrary executable or system file.
    magic=$(od -An -v -t x1 -N 11 -- "$appimage" | tr -d '[:space:]')
    [[ $magic == 7f454c46????????414902 ]] || fail 'The target does not look like an AppImage.'
    original=$(stat -c '%d:%i:%s:%y:%z' -- "$appimage")
else
    [[ ${HOME:-} = /* ]] || fail 'HOME must be an absolute path.'
    data=${XDG_DATA_HOME:-"$HOME/.local/share"}
    [[ $data = /* ]] || fail 'XDG_DATA_HOME must be an absolute path.'
    install_dir="$data/cadence/appimage"
    appimage="$install_dir/$asset"
    desktop_dir="$data/applications"
    launcher="$desktop_dir/dev.cadence.app.AppImage.desktop"
    command_path="$HOME/.local/bin/cadence"
fi
case $appimage in
    *$'\n'*|*$'\r'*|*$'\t'*) fail 'The install path cannot contain tabs or line breaks.' ;;
esac

base=https://github.com/MORT1SXD/cadence-linux/releases
if [[ $version == latest ]]; then
    release_url="$base/latest/download"
else
    release_url="$base/download/$version"
fi

mkdir -p -- "$install_dir"
stage=$(mktemp -d "$install_dir/.install.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
download() {
    curl --disable --fail --silent --show-error --location \
        --proto '=https' --proto-redir '=https' --connect-timeout 15 --max-time 300 \
        --output "$stage/$1" "$release_url/$1"
}

printf 'Downloading Cadence (%s)…\n' "$version"
download SHA256SUMS || fail 'Could not download the release checksum.'
download "$asset" || fail 'Could not download the AppImage.'
digest=$(awk -v asset="$asset" 'NF==2 && $2==asset {print $1}' "$stage/SHA256SUMS")
[[ $digest =~ ^[[:xdigit:]]{64}$ ]] || fail 'The release checksum is missing or invalid.'
(cd "$stage" && printf '%s  %s\n' "$digest" "$asset" | sha256sum --check --status) ||
    fail 'Checksum mismatch. The existing install was not changed.'

if [[ -n $replace ]]; then
    # Update the exact executable even if it was renamed. Replacing it by
    # rename leaves the running AppImage's original inode intact until exit.
    [[ ! -L $appimage && -f $appimage && $(stat -c '%d:%i:%s:%y:%z' -- "$appimage") == "$original" ]] ||
        fail 'The existing AppImage changed during download. Nothing was replaced.'
    if cmp -s -- "$stage/$asset" "$appimage"; then
        printf '%s\n' 'Already up to date.'
        exit 0
    fi
    chmod --reference="$appimage" "$stage/$asset"
    chmod u-s,g-s,-t "$stage/$asset"
    mv -T -- "$stage/$asset" "$appimage"
    printf 'Updated: %s\nQuit and reopen Cadence to use the new version.\n' "$appimage"
    exit 0
fi

chmod 0755 "$stage/$asset"
mv -T -- "$stage/$asset" "$appimage"

mkdir -p -- "$HOME/.local/bin" "$desktop_dir"
if [[ ! -e $command_path && ! -L $command_path ]]; then
    ln -s -- "$appimage" "$command_path"
elif [[ ! -L $command_path || $(readlink "$command_path") != "$appimage" ]]; then
    printf 'Kept your existing %s unchanged.\n' "$command_path"
fi

marker='# Managed by the Cadence AppImage installer'
if [[ -L $launcher || ( -e $launcher && ! -f $launcher ) ]] ||
   [[ -f $launcher && $(head -n 1 -- "$launcher") != "$marker" ]]; then
    printf 'Kept your existing desktop launcher unchanged.\n'
else
    # Escape once for Exec argument quoting, then for the desktop-file string.
    exec_path=${appimage//\\/\\\\}
    exec_path=${exec_path//\"/\\\"}
    exec_path=${exec_path//\$/\\\$}
    exec_path=${exec_path//\`/\\\`}
    exec_path=${exec_path//\\/\\\\}
    exec_path=${exec_path//%/%%}
    extra=
    if "$extract"; then extra=' --appimage-extract-and-run'; fi
    # Keep extraction mode when updating a launcher previously installed with it.
    if [[ -z $extra && -f $launcher ]] && grep -q '^X-Cadence-Extract=true$' "$launcher"; then
        extract=true
        extra=' --appimage-extract-and-run'
    fi
    cat > "$stage/cadence.desktop" <<EOF
$marker
[Desktop Entry]
Type=Application
Name=Cadence
Comment=Send a custom chatbox to VRChat over OSC
Exec="$exec_path"$extra
Icon=applications-utilities
Categories=Utility;
Terminal=false
X-Cadence-Extract=$extract
EOF
    mv -T -- "$stage/cadence.desktop" "$launcher"
fi

printf 'Installed: %s\nQuit the old Cadence window before opening the updated app.\n' "$appimage"
