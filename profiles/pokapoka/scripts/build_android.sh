#!/usr/bin/env bash
# Builds the Felyne Village G native port as an Android APK (arm64-v8a,
# Android 10+, Vulkan 1.1), from an Arch Linux (or any Linux) host.
#
#   build_android.sh [--iso <image.iso>] [--jobs N] [--sdk-setup]
#
#   --iso PATH   your own ULJM-05881 image, to prepare the game code the first
#                time (the APK itself carries no disc image: on its first start
#                the app asks for your .iso through Android's file picker)
#   --jobs N     parallel compile jobs (default as in build_linux.sh)
#   --sdk-setup  download the Android command-line tools, platform 35,
#                build-tools 35.0.0 and NDK r28c into ANDROID_HOME
#                (default ~/Android/Sdk) and accept their licences
#
# Needs: a JDK 17+ (pacman -S jdk17-openjdk), cmake, ninja, python, curl,
# unzip. Output: dist/FelyneVillageG-android-arm64.apk, signed with your own
# key (KEYSTORE/KEYSTORE_PASS/KEY_ALIAS) or a throwaway one made in the build
# directory. Keep a key of your own if you want later builds to install as
# updates without losing the app's data.
#
# The APK contains code recompiled from YOUR copy of the game. It is for your
# own devices: do not share it.
set -euo pipefail

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"
# shellcheck source=../packaging/linux/sources.sh
source "$profile_dir/packaging/linux/sources.sh"

ndk_version="28.2.13676358" # r28c, the NDK Yakumo's Android build is tested with
platform_level=35           # the SDK platform the APK is linked against
min_api=29                  # Android 10: the oldest system the APK installs on
iso=""
jobs=""
sdk_setup=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --iso) iso="${2:?--iso needs a path}"; shift 2 ;;
        --jobs) jobs="${2:?--jobs needs a number}"; shift 2 ;;
        --sdk-setup) sdk_setup=1; shift ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
done
step() { printf '\n=== %s\n' "$*"; }
if [[ -z "$jobs" ]]; then
    cpus="$(nproc 2> /dev/null || echo 2)"
    mem_kib="$(awk '/MemTotal/ { print $2 }' /proc/meminfo 2> /dev/null || echo 4194304)"
    jobs=$((mem_kib / 2621440))
    [[ $jobs -le $cpus ]] || jobs=$cpus
    [[ $jobs -ge 1 ]] || jobs=1
fi

export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
if [[ $sdk_setup -eq 1 ]]; then
    step "Installing the Android SDK pieces into $ANDROID_HOME"
    command -v javac > /dev/null || { echo "error: install a JDK first (pacman -S jdk17-openjdk)" >&2; exit 1; }
    tools="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
    if [[ ! -x "$tools" ]]; then
        mkdir -p "$ANDROID_HOME/cmdline-tools"
        # Google's command-line tools for Linux; sdkmanager updates itself.
        # CMDLINE_TOOLS_URL overrides the build if Google has retired it.
        curl -fsSL -o /tmp/cmdline-tools.zip "${CMDLINE_TOOLS_URL:-https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip}"
        rm -rf "$ANDROID_HOME/cmdline-tools/latest" "$ANDROID_HOME/cmdline-tools/cmdline-tools"
        unzip -q /tmp/cmdline-tools.zip -d "$ANDROID_HOME/cmdline-tools"
        mv "$ANDROID_HOME/cmdline-tools/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
    fi
    yes | "$tools" --licenses > /dev/null || true
    "$tools" "platforms;android-$platform_level" "build-tools;35.0.0" "ndk;$ndk_version" "platform-tools"
fi
[[ -d "$ANDROID_HOME/ndk/$ndk_version" ]] ||
    { echo "error: NDK $ndk_version not in $ANDROID_HOME/ndk; run with --sdk-setup" >&2; exit 1; }
[[ -d "$ANDROID_HOME/platforms" ]] || { echo "error: no SDK platform; run with --sdk-setup" >&2; exit 1; }
command -v javac > /dev/null || { echo "error: javac not found (pacman -S jdk17-openjdk)" >&2; exit 1; }
toolchain="$ANDROID_HOME/ndk/$ndk_version/build/cmake/android.toolchain.cmake"

# --- Game code -----------------------------------------------------------------
if [[ -n "$iso" ]]; then
    step "Preparing the game from $iso"
    "$profile_dir/scripts/prepare_game.sh" "$iso"
fi
[[ -f "$profile_dir/game/EBOOT.ELF" ]] ||
    { echo "error: no game/EBOOT.ELF; pass --iso <your ULJM-05881 image> once" >&2; exit 1; }

work="$repo_dir/out/pokapoka-android-work"
sources="$work/sources"
deps="$work/deps"
mkdir -p "$sources" "$deps"
fetch() {
    local target="$sources/$1"
    if [[ -f "$target" ]] && echo "$3  $target" | sha256sum -c --status; then return; fi
    curl -fsSL -o "$target.part" "$2"
    echo "$3  $target.part" | sha256sum -c --status ||
        { echo "error: $2 does not match its pinned SHA-256" >&2; exit 1; }
    mv "$target.part" "$target"
}

# The analyzer and generator run on this machine, so generate with a host
# build of the tools first (they are game-neutral and quick to build).
step "Generating the recompiled code"
cmake -S "$repo_dir" -B "$repo_dir/out/pokapoka-tools" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPSPRECOMP_PROFILE="" > /dev/null
"$profile_dir/scripts/generate.sh" "$repo_dir/out/pokapoka-tools"

step "SDL3 $SDL3_VERSION for Android $min_api"
fetch "SDL3-$SDL3_VERSION.tar.gz" "$SDL3_URL" "$SDL3_SHA256"
fetch NotoSansCJKjp-Regular.otf "$NOTO_CJK_URL" "$NOTO_CJK_SHA256"
fetch NotoSansCJK-LICENSE.txt "$NOTO_CJK_LICENSE_URL" "$NOTO_CJK_LICENSE_SHA256"
sdl_source="$deps/SDL3-$SDL3_VERSION"
sdl_install="$deps/sdl3-$SDL3_VERSION-android"
stamp="$SDL3_VERSION android-$min_api $ndk_version"
if [[ ! -f "$sdl_install/.stamp" || "$(cat "$sdl_install/.stamp")" != "$stamp" ]]; then
    rm -rf "$sdl_source" "$deps/sdl3-build" "$sdl_install"
    tar -xzf "$sources/SDL3-$SDL3_VERSION.tar.gz" -C "$deps"
    cmake -S "$sdl_source" -B "$deps/sdl3-build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$min_api" -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DCMAKE_INSTALL_PREFIX="$sdl_install" > "$deps/sdl3-configure.log"
    cmake --build "$deps/sdl3-build" -j "$jobs" > "$deps/sdl3-build.log"
    cmake --install "$deps/sdl3-build" > /dev/null
    echo "$stamp" > "$sdl_install/.stamp"
fi

build_dir="$repo_dir/out/pokapoka-android"
step "Configuring $build_dir"
command -v glslangValidator > /dev/null || { echo "error: glslangValidator not found (pacman -S glslang)" >&2; exit 1; }
cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$min_api" -DCMAKE_BUILD_TYPE=Release \
    -DPSPRECOMP_PROFILE=pokapoka -DMHP3RD_ANDROID_APP=ON -DMHP3RD_RELEASE=ON \
    -DMHP3RD_FFMPEG=bundled -DMHP3RD_FFMPEG_DOWNLOAD_DIR="$sources" \
    -DPSPRECOMP_GENERATED_JOBS="$jobs" \
    -DSDL3_DIR="$sdl_install/lib/cmake/SDL3" -DCMAKE_FIND_ROOT_PATH="$sdl_install" | tee "$work/configure.log"
grep -q "Vulkan renderer enabled" "$work/configure.log" ||
    { echo "error: the Vulkan renderer is disabled for Android (SDL3 or glslang not found)" >&2; exit 1; }

step "Building libmain.so (about 1-3 hours the first time; resumable)"
cmake --build "$build_dir" --target Yakumo -j "$jobs"

step "Packing the APK"
mkdir -p "$repo_dir/dist"
apk="$repo_dir/dist/FelyneVillageG-android-arm64.apk"
rm -f "$apk" "$apk.idsig"
VERSION_NAME="${VERSION_NAME:-0.1-felyne}" FONT_DIR="$sources" ANDROID_NDK="$ANDROID_HOME/ndk/$ndk_version" \
    "$profile_dir/packaging/android/build_apk.sh" "$build_dir" "$sdl_source" "$sdl_install/lib/libSDL3.so" "$apk"
rm -f "$apk.idsig"
if unzip -l "$apk" | grep -Ei '\.(iso|cso|elf|pbp|prx)$|EBOOT|PARAM\.SFO|POKAPOKA\.'; then
    echo "error: the APK holds a game file" >&2
    exit 1
fi
echo
echo "$apk ($(du -h "$apk" | cut -f1))"
echo "Install: adb install -r \"$apk\"  (or copy it to the phone and open it)"
