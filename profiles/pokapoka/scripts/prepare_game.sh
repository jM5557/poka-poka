#!/usr/bin/env bash
# Populate profiles/pokapoka/game from your own disc image of
# Monster Hunter Diary: Poka Poka Felyne Village G (ULJM-05881).
#
#   prepare_game.sh <image.iso> [--copy]
#
# The game's code is PSP_GAME/USRDIR/MHi2_Master.prx, which the disc stores
# unencrypted (SYSDIR/EBOOT.BIN is only a small loader that starts it). It is
# extracted as game/EBOOT.ELF, the name the host and generate.sh expect. The
# image itself is linked as game/disc.iso (copied with --copy) and read in
# place for disc0:. Nothing from the game is ever committed.
set -euo pipefail

usage() { echo "usage: $0 <image.iso> [--copy]" >&2; exit 2; }
[[ $# -ge 1 && $# -le 2 ]] || usage
iso="$1"
copy=0
[[ "${2:-}" == "--copy" ]] && copy=1
[[ $# -eq 2 && $copy -eq 0 ]] && usage
[[ -f "$iso" ]] || { echo "error: $iso not found" >&2; exit 1; }

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
game_dir="$profile_dir/game"
expected_sha256="3af4813ee99f79d93dded2cad96dba13823d1dce3dd66e8d47318bec6f047416"
sha256() { if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
python3 "$profile_dir/tools/extract_iso.py" "$iso" "$work" /PSP_GAME/USRDIR/MHi2_Master.prx /PSP_GAME/PARAM.SFO \
    > /dev/null
prx="$(find "$work" -name MHi2_Master.prx | head -1)"
sfo="$(find "$work" -name PARAM.SFO | head -1)"
[[ -n "$prx" && -n "$sfo" ]] ||
    { echo "error: $iso has no PSP_GAME/USRDIR/MHi2_Master.prx; is it Felyne Village G?" >&2; exit 1; }
if ! grep -aq "ULJM05881" "$sfo"; then
    echo "error: PARAM.SFO does not name ULJM05881; only the Japanese Felyne Village G is supported" >&2
    exit 1
fi
actual="$(sha256 "$prx")"
if [[ "$actual" != "$expected_sha256" ]]; then
    echo "error: MHi2_Master.prx has SHA-256 $actual, not the supported $expected_sha256" >&2
    echo "       (disc version 1.01 is the one this profile was made from)" >&2
    exit 1
fi

mkdir -p "$game_dir/ms0"
cp "$prx" "$game_dir/EBOOT.ELF"
rm -f "$game_dir/disc.iso"
iso_abs="$(cd "$(dirname "$iso")" && pwd)/$(basename "$iso")"
if [[ $copy -eq 1 ]]; then
    cp "$iso_abs" "$game_dir/disc.iso"
else
    ln -s "$iso_abs" "$game_dir/disc.iso"
fi
echo "game data prepared in $game_dir"
