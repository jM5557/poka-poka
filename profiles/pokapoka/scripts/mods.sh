#!/usr/bin/env bash
# Turns Felyne Village G disc mods on and off.
#
#   mods.sh [--data-dir DIR] list
#   mods.sh [--data-dir DIR] enable NAME
#   mods.sh [--data-dir DIR] disable NAME
#   mods.sh [--data-dir DIR] off            (disable all)
#
# Mods are DIR/disc_mods/NAME.pkdm (tools/make_disc_mod.py makes them) and the
# enabled ones are listed in DIR/disc_mods/enabled.txt, read when the game
# starts. DIR is the folder that holds ms0/: profiles/pokapoka/game for a build
# run from the checkout (the default when it exists), or the per-user data
# folder of a release build or the AppImage, ~/.local/share/Yakumo/PokaPokaG.
# For one run without changing the list: MHP3RD_DISC_MODS=none (or =NAME,...).
set -euo pipefail

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
data_dir=""
if [[ "${1:-}" == "--data-dir" ]]; then
    data_dir="${2:?--data-dir needs a folder}"
    shift 2
fi
if [[ -z "$data_dir" ]]; then
    if [[ -d "$profile_dir/game/ms0" ]]; then
        data_dir="$profile_dir/game"
    else
        data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/Yakumo/PokaPokaG"
    fi
fi
mods="$data_dir/disc_mods"
list="$mods/enabled.txt"
mkdir -p "$mods"
touch "$list"

valid() { [[ "$1" =~ ^[A-Za-z0-9._-]+$ && "$1" != *..* ]]; }

case "${1:-list}" in
    list)
        echo "disc mods in $mods:"
        found=0
        for file in "$mods"/*.pkdm; do
            [[ -e "$file" ]] || continue
            found=1
            name="$(basename "$file" .pkdm)"
            if grep -qxF "$name" "$list"; then state="on"; else state="off"; fi
            printf '  %-24s %s  (%s)\n' "$name" "$state" "$(du -h "$file" | cut -f1)"
        done
        [[ $found -eq 1 ]] || echo "  none; make one with tools/make_disc_mod.py"
        ;;
    enable)
        name="${2:?enable needs a mod name}"
        valid "$name" || { echo "error: bad mod name" >&2; exit 2; }
        [[ -f "$mods/$name.pkdm" ]] || { echo "error: $mods/$name.pkdm not found" >&2; exit 1; }
        grep -qxF "$name" "$list" || echo "$name" >> "$list"
        echo "$name is on from the next start"
        ;;
    disable)
        name="${2:?disable needs a mod name}"
        valid "$name" || { echo "error: bad mod name" >&2; exit 2; }
        grep -vxF "$name" "$list" > "$list.new" || true
        mv "$list.new" "$list"
        echo "$name is off from the next start"
        ;;
    off)
        : > "$list"
        echo "all disc mods are off from the next start"
        ;;
    *)
        sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'
        exit 2
        ;;
esac
