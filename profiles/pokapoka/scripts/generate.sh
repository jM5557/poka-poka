#!/usr/bin/env bash
# Analyze MHi2_Master.prx and regenerate the recompiled C++.
#
#   generate.sh [build_dir]
#
# Requires a configured build directory (default out/pokapoka) and
# game/EBOOT.ELF from prepare_game.sh. Output: profiles/pokapoka/analysis and
# profiles/pokapoka/generated, both ignored by Git.
set -euo pipefail

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"
build_dir="${1:-$repo_dir/out/pokapoka}"
elf="$profile_dir/game/EBOOT.ELF"

if [[ ! -f "$elf" ]]; then
    echo "missing $elf; run scripts/prepare_game.sh first" >&2
    exit 1
fi

cmake --build "$build_dir" --target psp_analyze psp_recomp
mkdir -p "$profile_dir/analysis"
"$build_dir/psp_analyze" "$elf" "$profile_dir/analysis/report.json"
# The module is relocatable; it is loaded (and so generated) at the default
# user base 0x08804000, the same base the host loads it at. psp_recomp rewrites
# only units whose text changed, so a rerun recompiles nothing that is current.
"$build_dir/psp_recomp" "$elf" --auto "$profile_dir/generated"
