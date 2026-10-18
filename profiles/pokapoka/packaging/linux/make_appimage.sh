#!/usr/bin/env bash
# Packs a staged build (scripts/build_linux.sh) into an AppImage.
#
#   make_appimage.sh <stage dir> <output.AppImage>
#
# The stage holds Yakumo, lib/ (FFmpeg, SDL3), fonts/ and licenses/. They go
# to usr/lib/felyne-village-g in the AppDir; AppRun raises the stack limit the
# recompiled code needs and starts the program. The Vulkan loader and GPU
# drivers always come from the host system.
#
# appimagetool is taken from PATH (AUR: appimagetool-bin), from APPIMAGETOOL,
# or downloaded once from the AppImage project's GitHub releases.
set -euo pipefail

stage="$(cd "${1:?usage: make_appimage.sh <stage dir> <output.AppImage>}" && pwd)"
output="${2:?usage: make_appimage.sh <stage dir> <output.AppImage>}"
here="$(cd "$(dirname "$0")" && pwd)"
id="felyne-village-g"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
appdir="$work/FelyneVillageG.AppDir"
mkdir -p "$appdir/usr/lib/$id" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/256x256/apps"
cp -a "$stage/." "$appdir/usr/lib/$id/"

cat > "$appdir/AppRun" << 'EOF'
#!/bin/sh
# Felyne Village G (native port, Yakumo/PSPRecomp based).
here="$(dirname "$(readlink -f "$0")")"
program="$here/usr/lib/felyne-village-g"
# Recompiled code nests native calls deeply: the main thread needs 64 MiB.
ulimit -s 65536 2> /dev/null || true
export LD_LIBRARY_PATH="$program/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Game Mode on a Steam Deck: SDL picks Wayland or X11 by itself.
exec "$program/Yakumo" "$@"
EOF
chmod 755 "$appdir/AppRun"

install -m 644 "$here/$id.desktop" "$appdir/$id.desktop"
install -m 644 "$here/$id.desktop" "$appdir/usr/share/applications/$id.desktop"
python3 "$here/make_icon.py" "$appdir/$id.png"
cp "$appdir/$id.png" "$appdir/usr/share/icons/hicolor/256x256/apps/$id.png"
ln -s "$id.png" "$appdir/.DirIcon"

tool="${APPIMAGETOOL:-$(command -v appimagetool || true)}"
if [[ -z "$tool" ]]; then
    cache="${XDG_CACHE_HOME:-$HOME/.cache}/felyne-village-g"
    tool="$cache/appimagetool-x86_64.AppImage"
    if [[ ! -x "$tool" ]]; then
        mkdir -p "$cache"
        echo "downloading appimagetool"
        curl -fsSL -o "$tool.part" \
            "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
        chmod 755 "$tool.part"
        mv "$tool.part" "$tool"
    fi
fi

mkdir -p "$(dirname "$output")"
# APPIMAGE_EXTRACT_AND_RUN: works without FUSE (containers, some SteamOS setups).
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream "$appdir" "$output"
echo
echo "$output ($(du -h "$output" | cut -f1))"
echo "Copy it anywhere (on a Steam Deck: Desktop Mode, then 'Add a Non-Steam Game' for Game Mode)."
