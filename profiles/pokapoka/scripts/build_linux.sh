#!/usr/bin/env bash
# One-stop Linux build of the Felyne Village G native port (profile
# "pokapoka"): dependencies, game preparation, code generation, the build and,
# optionally, an AppImage for Linux desktops and SteamOS.
#
#   build_linux.sh --iso <image.iso> [options]
#
#   --iso PATH       your own ULJM-05881 disc image (needed the first time)
#   --deps           install the build dependencies with pacman (Arch Linux)
#   --jobs N         parallel jobs (default: number of CPUs, capped by RAM)
#   --release        a release build: no checkout paths, the first start sets
#                    the game up from the player's .iso (as an AppImage does)
#   --appimage       a release build packed as dist/FelyneVillageG-x86_64.AppImage
#   --sdk            build inside the Steam Runtime 3 "sniper" SDK container
#                    (podman or docker), so the AppImage runs on SteamOS and
#                    any distribution with glibc 2.31 or newer. Implies --appimage.
#   --build-dir DIR  build directory (default out/pokapoka, out/pokapoka-release
#                    for release builds)
#   --opt N          optimization of the generated code: 2 (default, fastest
#                    game) or 1 (builds in about half the time, runs slower)
#   --ffmpeg MODE    bundled (default: download and build FFmpeg 7.1.5 with only
#                    the decoders the game needs), system (pacman -S ffmpeg) or OFF
#
# A plain developer build runs from the checkout:
#   out/pokapoka/bin/Yakumo profiles/pokapoka/game
#
# The AppImage contains code recompiled from YOUR copy of the game. It is for
# your own use: do not share it.
set -euo pipefail

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"
# shellcheck source=../packaging/linux/sources.sh
source "$profile_dir/packaging/linux/sources.sh"

iso=""
deps=0
release=0
appimage=0
sdk=0
in_container=0
build_dir=""
jobs=""
ffmpeg=bundled
opt=2
while [[ $# -gt 0 ]]; do
    case "$1" in
        --iso) iso="${2:?--iso needs a path}"; shift 2 ;;
        --deps) deps=1; shift ;;
        --jobs) jobs="${2:?--jobs needs a number}"; shift 2 ;;
        --release) release=1; shift ;;
        --appimage) release=1; appimage=1; shift ;;
        --sdk) sdk=1; release=1; appimage=1; shift ;;
        --in-container) in_container=1; shift ;; # internal: set by --sdk
        --build-dir) build_dir="${2:?--build-dir needs a directory}"; shift 2 ;;
        --opt) opt="${2:?--opt needs 1 or 2}"; shift 2 ;;
        --ffmpeg) ffmpeg="${2:?--ffmpeg needs bundled, system or OFF}"; shift 2 ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
done

step() { printf '\n=== %s\n' "$*"; }

# Generated units need about 0.5-2 GiB each: one job per 2.5 GiB of RAM.
if [[ -z "$jobs" ]]; then
    cpus="$(nproc 2> /dev/null || echo 2)"
    mem_kib="$(awk '/MemTotal/ { print $2 }' /proc/meminfo 2> /dev/null || echo 4194304)"
    by_mem=$((mem_kib / 2621440))
    jobs=$((cpus < by_mem ? cpus : by_mem))
    [[ $jobs -ge 1 ]] || jobs=1
fi

# --- Steam Runtime container -------------------------------------------------
if [[ $sdk -eq 1 && $in_container -eq 0 ]]; then
    engine="$(command -v podman || command -v docker || true)"
    [[ -n "$engine" ]] || { echo "error: --sdk needs podman or docker (pacman -S podman)" >&2; exit 1; }
    if [[ -n "$iso" ]]; then
        "$profile_dir/scripts/prepare_game.sh" "$iso"
    fi
    [[ -f "$profile_dir/game/EBOOT.ELF" ]] || { echo "error: run once with --iso first" >&2; exit 1; }
    step "Building inside $SDK_IMAGE"
    mkdir -p "$repo_dir/out"
    # The image is not needed inside: the release build reads only EBOOT.ELF.
    "$engine" run --rm --network=host \
        -v "$repo_dir:$repo_dir" -w "$repo_dir" \
        -e HOME=/tmp -e YAKUMO_JOBS="$jobs" \
        "$SDK_IMAGE" \
        bash "$profile_dir/scripts/build_linux.sh" --in-container --appimage --jobs "$jobs" --ffmpeg "$ffmpeg" --opt "$opt" \
        --build-dir "${build_dir:-$repo_dir/out/pokapoka-sdk}"
    exit 0
fi

# --- Dependencies ------------------------------------------------------------
if [[ $deps -eq 1 ]]; then
    step "Installing build dependencies (pacman)"
    if ! command -v pacman > /dev/null; then
        echo "error: --deps uses pacman; on other systems install: cmake ninja python3 SDL3 Vulkan headers" \
            "and loader, glslang, ccache, pkg-config, curl, a C/C++20 compiler" >&2
        exit 1
    fi
    if grep -q '^ID=steamos' /etc/os-release 2> /dev/null; then
        echo "error: SteamOS has a read-only system; build on another Linux machine with --sdk," \
            "or inside distrobox, and copy the AppImage to the Deck" >&2
        exit 1
    fi
    sudo pacman -S --needed --noconfirm base-devel cmake ninja python sdl3 vulkan-headers vulkan-icd-loader \
        glslang ccache pkgconf curl git file
fi

# --- Game data -----------------------------------------------------------------
if [[ -n "$iso" ]]; then
    step "Preparing the game from $iso"
    "$profile_dir/scripts/prepare_game.sh" "$iso"
fi
[[ -f "$profile_dir/game/EBOOT.ELF" ]] ||
    { echo "error: no game/EBOOT.ELF; pass --iso <your ULJM-05881 image> once" >&2; exit 1; }

# --- SDL3 inside the container ------------------------------------------------
cmake_extra=()
work="$repo_dir/out/pokapoka-work"
sources="$work/sources"
mkdir -p "$sources"
fetch() {
    local target="$sources/$1"
    if [[ -f "$target" ]] && echo "$3  $target" | sha256sum -c --status; then return; fi
    curl -fsSL -o "$target.part" "$2"
    echo "$3  $target.part" | sha256sum -c --status ||
        { echo "error: $2 does not match its pinned SHA-256" >&2; exit 1; }
    mv "$target.part" "$target"
}
if [[ $in_container -eq 1 ]]; then
    export CC=gcc-14 CXX=g++-14
    deps_dir="$work/deps"
    if [[ ! -f "$deps_dir/.sdl3-$SDL3_VERSION" ]]; then
        step "Building SDL3 $SDL3_VERSION for the Steam Runtime"
        fetch "SDL3-$SDL3_VERSION.tar.gz" "$SDL3_URL" "$SDL3_SHA256"
        rm -rf "$work/sdl3-src" "$work/sdl3-build"
        mkdir -p "$work/sdl3-src"
        tar -xzf "$sources/SDL3-$SDL3_VERSION.tar.gz" -C "$work/sdl3-src" --strip-components=1
        cmake -S "$work/sdl3-src" -B "$work/sdl3-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$deps_dir" -DCMAKE_INSTALL_LIBDIR=lib \
            -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
        cmake --build "$work/sdl3-build" -j "$jobs"
        cmake --install "$work/sdl3-build"
        touch "$deps_dir/.sdl3-$SDL3_VERSION"
    fi
    cmake_extra+=(-DCMAKE_PREFIX_PATH="$deps_dir")
    export PKG_CONFIG_PATH="$deps_dir/lib/pkgconfig"
fi

# --- Configure, generate, build ----------------------------------------------
if [[ -z "$build_dir" ]]; then
    build_dir="$repo_dir/out/pokapoka"
    [[ $release -eq 1 ]] && build_dir="$repo_dir/out/pokapoka-release"
fi
step "Configuring $build_dir"
cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPSPRECOMP_PROFILE=pokapoka \
    -DMHP3RD_RELEASE="$([[ $release -eq 1 ]] && echo ON || echo OFF)" \
    -DMHP3RD_FFMPEG="$ffmpeg" \
    -DMHP3RD_FFMPEG_DOWNLOAD_DIR="$sources" \
    -DPSPRECOMP_GENERATED_JOBS="$jobs" \
    -DPSPRECOMP_GENERATED_OPT_LEVEL="$opt" \
    "${cmake_extra[@]}" | tee "$build_dir.configure.log"
grep -q "Vulkan renderer enabled" "$build_dir.configure.log" ||
    { echo "error: the Vulkan renderer is disabled: SDL3, Vulkan or glslang was not found" >&2; exit 1; }

step "Generating the recompiled code (a few minutes)"
"$profile_dir/scripts/generate.sh" "$build_dir"
# The glob of generated units is read at configure time.
cmake "$build_dir" > /dev/null

step "Building (about 1-3 hours the first time; resumable)"
cmake --build "$build_dir" --target Yakumo -j "$jobs"

if [[ $appimage -eq 0 ]]; then
    echo
    if [[ $release -eq 1 ]]; then
        echo "Built $build_dir/bin/Yakumo (release: the first start asks for your .iso)"
    else
        echo "Built. Run:  $build_dir/bin/Yakumo $profile_dir/game"
    fi
    exit 0
fi

# --- Stage and pack the AppImage ---------------------------------------------
step "Staging"
stage="$work/stage/felyne"
rm -rf "$stage"
mkdir -p "$stage/lib" "$stage/fonts" "$stage/licenses"
# Yakumo's own menus fall back to Noto Sans CJK when the system has no CJK
# font; the game draws its text with its own font. Pinned download first, then
# the system's copy (pacman -S noto-fonts-cjk), else without it.
if (fetch NotoSansCJKjp-Regular.otf "$NOTO_CJK_URL" "$NOTO_CJK_SHA256" &&
    fetch NotoSansCJK-LICENSE.txt "$NOTO_CJK_LICENSE_URL" "$NOTO_CJK_LICENSE_SHA256"); then
    cp "$sources/NotoSansCJKjp-Regular.otf" "$stage/fonts/"
    cp "$sources/NotoSansCJK-LICENSE.txt" "$stage/licenses/NotoSansCJK-OFL.txt"
else
    system_font="$(fc-match -f '%{file}' 'Noto Sans CJK JP' 2> /dev/null || true)"
    if [[ "$system_font" == *NotoSansCJK* && -f "$system_font" ]]; then
        echo "note: using the system's $system_font for the menus' fallback font"
        cp "$system_font" "$stage/fonts/"
    else
        echo "warning: no Noto Sans CJK font bundled; Yakumo's menus use the system's fonts" >&2
    fi
fi
install -m 755 "$build_dir/bin/Yakumo" "$stage/Yakumo"
strip --strip-unneeded "$stage/Yakumo"
# The libraries the executable needs that are not part of every desktop:
# FFmpeg from the build, SDL3 from the container prefix or the system.
needed() { objdump -p "$1" | awk '$1 == "NEEDED" { print $2 }'; }
lib_dirs=("$build_dir/bin/lib")
[[ -n "${deps_dir:-}" ]] && lib_dirs+=("$deps_dir/lib")
pending=("$stage/Yakumo")
while [[ ${#pending[@]} -gt 0 ]]; do
    current="${pending[0]}"
    pending=("${pending[@]:1}")
    for soname in $(needed "$current"); do
        [[ -e "$stage/lib/$soname" ]] && continue
        found=""
        for dir in "${lib_dirs[@]}"; do [[ -e "$dir/$soname" ]] && found="$dir/$soname" && break; done
        # SDL3 from the system on a plain Arch build.
        if [[ -z "$found" && "$soname" == libSDL3* ]]; then
            found="$(ldconfig -p | awk -v n="$soname" '$1 == n { print $NF; exit }')"
        fi
        [[ -n "$found" ]] || continue
        cp -L "$found" "$stage/lib/$soname"
        chmod 644 "$stage/lib/$soname"
        strip --strip-unneeded "$stage/lib/$soname"
        pending+=("$stage/lib/$soname")
    done
done
# Built for the checkout? The release build looks for these next to itself.
patchelf_rpath="$(objdump -p "$stage/Yakumo" | awk '$1 == "RUNPATH" || $1 == "RPATH" { print $2 }')"
[[ "$patchelf_rpath" == *'$ORIGIN/lib'* ]] ||
    echo "warning: Yakumo has no \$ORIGIN/lib run path ($patchelf_rpath); the AppRun sets LD_LIBRARY_PATH" >&2
cp "$repo_dir/LICENSE" "$stage/licenses/Yakumo-LICENSE.txt"
cp "$profile_dir/packaging/THIRD_PARTY_NOTICES.md" "$stage/licenses/"
cp "$build_dir"/bin/lib/FFmpeg-*.txt "$stage/licenses/" 2> /dev/null || true
cp "$profile_dir/third_party/imgui/LICENSE.txt" "$stage/licenses/DearImGui-LICENSE.txt"
cp "$profile_dir/third_party/tiny_aes/UNLICENSE" "$stage/licenses/tiny-AES-c-UNLICENSE.txt"
cp "$profile_dir/third_party/xxhash/LICENSE" "$stage/licenses/xxHash-LICENSE.txt"

missing="$(LD_LIBRARY_PATH="$stage/lib" ldd "$stage/Yakumo" | grep 'not found' || true)"
[[ -z "$missing" ]] || { echo "error: unresolved libraries:" >&2; echo "$missing" >&2; exit 1; }
glibc_floor="$(objdump -T "$stage/Yakumo" "$stage"/lib/*.so* | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
echo "Newest glibc symbol needed: $glibc_floor"
if [[ $in_container -eq 0 ]]; then
    echo "note: built on this machine, so the AppImage needs at least its glibc ($glibc_floor)." \
        "For SteamOS use --sdk."
fi

step "Packing the AppImage"
mkdir -p "$repo_dir/dist"
"$profile_dir/packaging/linux/make_appimage.sh" "$stage" "$repo_dir/dist/FelyneVillageG-x86_64.AppImage"
