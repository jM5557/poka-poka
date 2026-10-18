# Poka-Poka: the Felyne Village G profile (`pokapoka`)

**Poka-Poka** is an experimental native port of **Monster Hunter Diary: Poka Poka Felyne Village G** (モンハン日記 ぽかぽかアイルー村G, Japan, `ULJM-05881`, disc version 1.01). It recompiles the game's PSP code ahead of time into C++ and runs it on a reimplementation of the PSP system. This folder, the `pokapoka` profile, is all of Poka-Poka that is specific to the game.

> [!NOTE]
> **Built on Yakumo, made for Felyne Village G only.** Poka-Poka is a fork of [Yakumo](https://github.com/TeamGDB/Yakumo), TeamGDB's port of *Monster Hunter Portable 3rd HD Ver.*, and of the [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp) framework under it. This profile started as a copy of Yakumo's `profiles/mhp3rd` and was adapted to Felyne Village G; it supports no other game. Poka-Poka is not affiliated with or endorsed by TeamGDB or the Yakumo authors, so please report its problems here, not to Yakumo.

> **Bring your own game.** Nothing from the game is in this repository. Every build generates its code from your own disc image, so the programs and packages you build (the executable, the AppImage, the APK) contain code derived from your copy: keep them for your own use and do not share them.

## Status

**Starts and plays (early).** On Linux with Intel Iris Xe (Mesa) the game shows its autosave notice, the logos and the opening movie, and gets through character creation, the first cutscene and into the village at 30 fps. Saving and loading, long play sessions, music streaming on real tracks, Android and the Steam Deck are not verified yet. [`BRINGUP.md`](BRINGUP.md) has what was fixed, how it was verified and what is known to be missing.

| Area | State |
| --- | --- |
| Code | All of `MHi2_Master.prx` recompiled: 22 541 functions, 229 units, no overlays (the game has none). The interpreter covers anything the analyzer missed |
| Imports | 280 across 30 libraries; MHP3rd's HLE plus the calls this game adds (async file I/O, lightweight mutexes, `sceKernelWaitThreadEnd`, alarms, UMD state, `sceDisplayWaitVblankStart`, controller peek, ATRAC half-way buffers, the game-data install, RTC ticks). Online functions (`sceHttp`, `sceNetApctl`, `sceNetInet`, ad hoc matching) are logging stubs |
| Memory | 32 MiB layout, as on a PSP-1000 |
| Data | `POKAPOKA.FAT/.BHD/.BND` read straight from the image; the optional data install copies `INSDIR` to the memory stick |
| Disabled | Every inherited Yakumo feature that patches MHP3rd's code at fixed addresses: analog camera, lock-on, free camera, HUD hiding, layered armor, aspect hooks, code overlays, MHP3rd text and mods |

## How this game differs from MHP3rd

- `PSP_GAME/SYSDIR/EBOOT.BIN` is only `mhi_parent_module`, a 57 KB loader (encrypted, tag `0xD91613F0`). It loads `PSP_GAME/USRDIR/MHi2_Master.prx`, the game, and `MHiBrowser_Master.prx`, the online DLC browser. **The game module is not encrypted**, so no decryption is needed: the host starts `MHi2_Master.prx` directly and skips the browser.
- It is a relocatable PRX, loaded and generated at `0x08804000`.
- It runs in 32 MiB and paces itself with `sceDisplayWaitVblankStart`.
- Music is streamed `.at3` through ATRAC half-way buffers.

| Item | Value |
| --- | --- |
| `MHi2_Master.prx` SHA-256 | `3af4813ee99f79d93dded2cad96dba13823d1dce3dd66e8d47318bec6f047416` |
| Module | `mhi_game_module` 1.1, entry `0x08A999D4` |
| Identity in code | `host/install/game_identity.hpp`, `config/pokapoka_uljm05881.toml` |

## Building on Arch Linux

```sh
git clone <this fork> poka-poka && cd poka-poka
profiles/pokapoka/scripts/build_linux.sh --deps --iso ~/Games/FelyneVillageG.iso
out/pokapoka/bin/Yakumo profiles/pokapoka/game   # Poka-Poka keeps Yakumo's program name
```

`--deps` installs `base-devel cmake ninja python sdl3 vulkan-headers vulkan-icd-loader glslang ccache pkgconf curl git file` with pacman. The first build takes one to three hours; later ones only recompile what changed, and ccache makes a second build directory (say, the AppImage's) take minutes. It downloads FFmpeg 7.1.5 once, checks its SHA-256 and builds it with only the ATRAC3/ATRAC3plus/H.264 decoders (`--ffmpeg system` uses Arch's `ffmpeg` instead). `--opt 1` halves the build time for testing, at some cost in speed; `--jobs N` overrides the parallelism the script picks from your CPU count and memory.

### AppImage (Linux desktops and SteamOS)

```sh
# Built against this machine's libraries: runs on this and newer systems.
profiles/pokapoka/scripts/build_linux.sh --appimage

# Built inside the Steam Runtime 3 "sniper" SDK container (podman or docker):
# runs on SteamOS / Steam Deck and anything with glibc 2.31 or newer.
sudo pacman -S podman
profiles/pokapoka/scripts/build_linux.sh --sdk
```

The result is `dist/FelyneVillageG-x86_64.AppImage`. On its first start it asks for your `.iso` (the first-run setup inherited from Yakumo) and keeps everything in `~/.local/share/Yakumo/PokaPokaG/`, apart from any installation of Yakumo itself. On a Steam Deck, copy it over in Desktop Mode, make it executable, and add it with *Add a Non-Steam Game* to play in Game Mode. A machine without FUSE can run it with `--appimage-extract-and-run`.

### Android

```sh
sudo pacman -S jdk17-openjdk unzip
profiles/pokapoka/scripts/build_android.sh --sdk-setup --iso ~/Games/FelyneVillageG.iso
adb install -r dist/FelyneVillageG-android-arm64.apk
```

`--sdk-setup` installs the Android command-line tools, platform 35, build-tools 35.0.0 and NDK r28c into `~/Android/Sdk` (or `ANDROID_HOME`). The APK is for arm64 phones with Android 10+ and Vulkan 1.1, installs as `io.github.felynevillage.g` (beside Yakumo itself, not over it) and asks for your `.iso` on its first start. Set `KEYSTORE`, `KEYSTORE_PASS` and `KEY_ALIAS` to sign with a key of your own, so that later builds install as updates without wiping the app's data.

### Scripts

| Script | Does |
| --- | --- |
| `scripts/prepare_game.sh <iso>` | Checks the disc id and the module hash, writes `game/EBOOT.ELF` (the game module) and links the image as `game/disc.iso` |
| `scripts/generate.sh [build]` | Runs `psp_analyze` and `psp_recomp` on the module into `generated/` |
| `scripts/build_linux.sh` | Dependencies, preparation, generation, build, optional AppImage |
| `scripts/build_android.sh` | Android SDK/NDK setup, SDL3 for Android, the build, the APK |
| `scripts/mods.sh` | Lists disc mods and turns them on and off |
| `scripts/diagnose.sh [seconds]` | One bounded run with traces, screenshots and scripted button presses, packed into an archive to send with a bug report |
| `tools/make_disc_mod.py` | Turns an xdelta patch for the disc image into a disc mod |
| `packaging/linux/make_appimage.sh` | Packs a staged build into an AppImage |

## In-game menu

Esc, or L3 + R3 on a gamepad, opens the Poka-Poka menu over the game and pauses it. It has five pages:

| Page | What is there |
| --- | --- |
| Video | Resolution, window, aspect (Original or Stretch), filters, texture packs, vsync, frame rate, game speed, fast loading and fast-forward, the performance overlay, GPU compatibility |
| Audio | Volume and output |
| Controls | Presets and bindings, chords, gamepads, confirm button, dead zone, the right stick as a D-pad, mouse buttons, the on-screen controls |
| Mods | Disc mods: switch each one on or off, then restart; opens the disc mods folder |
| System | Name entry, screenshots, pausing, the data and saves folders, setting up again, about |

Yakumo's QoL, Network and Debug pages are gone, together with the rest of what only Monster Hunter Portable 3rd could use: the analog camera and aim, mouse look, lock-on, layered armor, the free camera, HUD hiding, the Fill aspect, MHP3rd's fonts, translations and `DATA.BIN` mods, the Action touch layout, the camera, lock-on and item bindings, the Modern preset, and MHP3rd save import, export and backups. Those settings are kept off even when an old `settings.ini` or an `MHP3RD_*` variable turns them on (`keep_to_this_game` in `host/settings/settings.cpp`). The game has ad hoc functions, but ad hoc play is off. The program, its environment variables and its data folder still use Yakumo's names (`Yakumo`, `MHP3RD_*`, `~/.local/share/Yakumo/PokaPokaG`), so existing setups keep working.

## Disc mods (fan translations)

Patches made for the disc image, such as the English fan translation (an xdelta patch against the clean ISO), can be used without making a patched copy of the image, and turned on and off between runs. A disc mod is the set of 2048-byte sectors the patch changes plus a fingerprint of the sectors it replaces, so it is refused on any other image instead of corrupting it. The game's code is not touched by the translation, so the recompiled program needs nothing else.

```sh
sudo pacman -S xdelta3
# once: turn the patch into a mod (about 2 MB) next to your saves
profiles/pokapoka/tools/make_disc_mod.py ~/Games/FelyneVillageG.iso \
    PokaPoka_Ailu_Mura_G_EN_test.xdelta english \
    --expect-sha256 3C6A83F437D74B836E9425C08B2AD2ACB18BEE1BB1B88819B7436A3F1B53F28F
profiles/pokapoka/scripts/mods.sh enable english    # on from the next start
profiles/pokapoka/scripts/mods.sh disable english   # back to Japanese
profiles/pokapoka/scripts/mods.sh list                 # or the menu's Mods page
MHP3RD_DISC_MODS=none out/pokapoka/bin/Yakumo profiles/pokapoka/game   # one run without mods
```

**English spacing.** The translation leaves the space, punctuation and digits at the Japanese width of 15 pixels, so word gaps are doubled and lines overflow. `tools/font_fit.py <your translated image>` measures each Latin character in the game's font and writes `font_fit_out/english-font.pkdm`, a disc mod with natural spacing for that image (copy it to `disc_mods/` and switch it on under Mods). It changes only spacing values in `MainFont.ccm`; `tools/font_probe.py` describes the fonts without changing anything.

`make_disc_mod.py` writes to `profiles/pokapoka/game/disc_mods/` by default; for the AppImage or another release build, pass `--out ~/.local/share/Yakumo/PokaPokaG/disc_mods` and use `mods.sh --data-dir ~/.local/share/Yakumo/PokaPokaG`. The start-up log says `[disc-mod] english: N sectors replaced`, or why a mod was refused. Saves are the same with and without a text-only mod. Format and rules: `host/kernel/disc_mods.hpp`.

## Running and debugging

Yakumo's guide to its own profile, kept here as [`MHP3RD_HOST_REFERENCE.md`](MHP3RD_HOST_REFERENCE.md), describes the controls, settings, environment variables and tracing Poka-Poka inherits. All of it applies, under the same `MHP3RD_*` names, except the MHP3rd-only features listed above and under *In-game menu*. Useful for bring-up:

```sh
# Headless boot check, bounded
MHP3RD_NO_RENDER=1 MHP3RD_NO_AUDIO=1 timeout 60 out/pokapoka/bin/Yakumo profiles/pokapoka/game
# What the game reads and which unimplemented imports it reaches
MHP3RD_TRACE_IO=1 MHP3RD_NO_RENDER=1 timeout 60 out/pokapoka/bin/Yakumo profiles/pokapoka/game 2>&1 | grep -E '\[io\]|hle-stub'
```

Every unimplemented import logs `[hle-stub] Library::name` the first time it is called, with its arguments and return address.
