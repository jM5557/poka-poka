# Poka-Poka bring-up notes

Where Poka-Poka, the Felyne Village G port, stands, what was verified and how, and what to do next. Update this file as the game gets further.

> Poka-Poka is built on [Yakumo](https://github.com/TeamGDB/Yakumo) but targets only Monster Hunter Diary: Poka Poka Felyne Village G. Below, "Yakumo" means the code Poka-Poka inherited from it, as it was before these changes; the `Yakumo` program name and `Yakumo error:` messages are inherited names, not Yakumo itself.

## Verified (2026-10-13, Linux x86-64, GCC 13, this branch)

| Check | Result |
| --- | --- |
| `MHi2_Master.prx` from the disc | Plain ELF/PRX (`~PSP` only on `SYSDIR/EBOOT.BIN`), SHA-256 `3af4813e…7416` |
| `psp_analyze` | 22 541 functions, 139 677 relocations (none unsupported), 280 imports in 30 libraries, 32 MiB layout |
| `psp_recomp` | 229 units, 936 536 registered entry points, about 2 minutes |
| Full build | 61 minutes at `-j2`, `-O1` for generated code on a 2-core, 8 GB machine; the default `-O2` takes longer |
| Unit tests | `ctest`: 57 of 57 pass, including new `pokapoka_contracts` (lightweight mutexes, async I/O, thread end, RTC, memcpy, system strings, free memory). MHP3rd's camera and lock-on tests are disabled because those features are |
| Installer | `out/pokapoka/bin/Yakumo --install image.iso` accepts a `ULJM05881` image, extracts `MHi2_Master.prx`, checks its hash |
| `prepare_game.sh` | Accepts the image, checks disc id and hash, prepares `game/` |
| AppImage | `build_linux.sh --appimage --ffmpeg system --opt 1` end to end: release build, staging, AppImage (53 MB); the AppImage's `--install image.iso` sets the game up and it boots headless to the same point as below |
| Headless boot | See below |

Not verified: anything with a window, the GPU or sound; the bundled FFmpeg and the pinned font downloads (ffmpeg.org and GitHub raw files were unreachable from the test machine, so system FFmpeg and the system Noto font were used); the real disc's assets (the test image held only the files that were available: `SYSDIR/EBOOT.BIN`, `USRDIR/MHi2_Master.prx`, `USRDIR/MHiBrowser_Master.prx`, `PARAM.SFO`); the Android build (Google's SDK downloads were unreachable from the test machine); the `--sdk` container build; a Steam Deck.

## Headless boot, step by step

```
HLE imports: 280 total, 307 implemented, 46 logging stubs
Entry:      0x08A999D4
Functions:  936536
[memory] ignored guest access to null page address 0x00000000     (see 1)
[io] open msstor0p1:/ flags=0x00000003 -> 0x80010013               (see 2)
[guest] msm init OK                                                (the game's own debug print)
[io] getstat disc0:/PSP_GAME/USRDIR/POKAPOKA.BHD -> 0x80010002     (repeats: the file was not in the test image)
```

1. `module_start` calls a function (text+0x0) that stores `sceKernelGetThreadId()` through a null pointer, with no relocation: a bug in the game that a PSP ignores. The guest memory inherited from Yakumo stopped there. The framework now has `PSPRECOMP_NULL_PAGE=ignore` (reads 0, drops writes, logs the first 8, only below 0x10000), which this profile sets; `PSPRECOMP_NULL_PAGE=strict` restores the stop.
2. The game's memory-stick manager opens the raw device `msstor0p1:/`. The inherited host has no such device and returns "no device"; the game says `msm init OK` anyway. Watch this if saving or the data install misbehaves.

With the real image the game reads `POKAPOKA.BHD/.FAT/.BND` next. None of the 46 stubs was reached up to this point.

## Diagnostic runs

`scripts/diagnose.sh [seconds]` runs the game for a bounded time with the display, file, movie and save-data traces, a performance line per second, screenshots every 60 frames and a virtual gamepad pressing Start, ○ and ✕, then packs `diagnose-<time>.tar.gz` (log, `summary.txt`, screenshots). `MHP3RD_TRACE_DISPLAY=1` on its own logs the framebuffer the game shows.

On real hardware (Iris Xe, Mesa 26.1, 2026-10-13) the game's main loop runs at speed after the scratchpad and VFPU fixes: 30 fps, two 16-bit (5650) framebuffers at 0x04088000/0x040CC000, 10 draws a frame, all black. The first diagnostic run showed why: the game reads its archive through the raw `umd1:` block device, seeking to sector 236 544 and reading 129 sectors (the size of `POKAPOKA.BHD`), and the host inherited from Yakumo took those numbers as bytes. `umd0:`/`umd1:` (the device itself and its `sce_lbn` ranges) now count sectors.

The next stop (reproduced with the real `POKAPOKA.BHD`/`.FAT` and a placeholder `.BND`): the game's file-request handler (text 0x2f48a8) refuses every `disc0:`/`umd1:` request until a global UMD state reaches 4, which the game's UMD callback sets; the request loop polls `sceKernelCheckCallback`. The callback is now notified with the drive state on `sceUmdRegisterUMDCallBack` and `sceUmdActivate`, and the game goes on to read `POKAPOKA.BND` through async I/O.

## Later fixes (2026-10-13, verified by the player's runs)

| Stop | Fix |
| --- | --- |
| Opening movie never advanced | `sceMpegAvcDecode`/`sceMpegAvcDecodeStop` decode straight to RGBA8888 (MHP3rd used YCbCr + `sceJpegCsc`) |
| Movie at 9% speed | `MovieReadThread` polled `sceMpegRingbufferAvailableSize` ~150 000 times a frame while the ring was full; such a poll now sleeps 1 ms of emulated time (`MHP3RD_MPEG_NO_POLL_DELAY` to compare) |
| Music track larger than its buffer | ATRAC ring-buffer streaming: added chunks are copied to a host-side image of the file and decoded from there; the game always writes at the buffer start (unit-tested; not yet heard on a real track) |

After these the game reaches character creation, the first cutscene and the village at 30 fps, with no stub reached and no hang.

## Next steps (run with your real image)

```sh
ulimit -s 65536
MHP3RD_NO_RENDER=1 MHP3RD_NO_AUDIO=1 MHP3RD_TRACE_IO=1 timeout 120 \
    out/pokapoka/bin/Yakumo profiles/pokapoka/game > boot.log 2>&1
grep -v 'POKAPOKA.BND.*read' boot.log | head -200
```

Then with a window: `timeout 120 out/pokapoka/bin/Yakumo profiles/pokapoka/game`. Look for, in this order:

- **`[hle-stub]` lines**: an unimplemented import the game reached, with its arguments and caller. Implement it in `host/hle/`.
- **`Yakumo error: Guest memory access …`**: the guest PC and registers say where; `mips-linux-gnu-objdump -d -EL game/EBOOT.ELF --start-address=<pc-0x08804000>` shows the code.
- **`interpreted` addresses in the end-of-run report**: code the analyzer missed (function pointers); add them as seeds.
- **A hang**: `describe_threads` at exit lists every thread and what it waits for.
- **Graphics**: `MHP3RD_TRACE_GE=1`, `MHP3RD_SCREENSHOT_DIR=…`. The renderer was built and tuned for MHP3rd; expect missing GE features.

## Known gaps

| Area | Gap |
| --- | --- |
| Music | Half-way and ring-buffer streaming are handled; seeking far ahead of the data streamed so far is not |
| Sound effects | `__sceSasSetADSR`, `SetADSRmode`, `SetSL` and `SetNoise` are accepted but not modelled |
| Alarms | `sceKernelSetAlarm` fires at vblank resolution (16.7 ms) |
| `sceIoIoctl` | Logged and answered with 0; implement the commands the game uses |
| Data install | Copies `INSDIR` to `ms0:/PSP/SAVEDATA/ULJM05881DATAINSTALL/` without parsing the parameter block; the game may expect a `PARAM.SFO` there |
| Online | `sceHttp`, `sceNetApctl`, `sceNetInet` and `sceNetAdhocMatching` are logging stubs that return 0; avoid the online menus. `MHiBrowser_Master.prx` is never loaded |
| Ad hoc | The ad hoc code inherited from Yakumo is made for MHP3rd; this game's matching library is not implemented |
| Controls | The controller sample still fills bytes 10-11 (the PS3 release's second stick) with 0x80; on a PSP-1000 game they are reserved |
| MHP3rd features | Analog camera, lock-on, free camera, HUD hiding, layered armor, aspect hooks, `DATA.BIN` mods and text translation patch MHP3rd addresses: removed from the menu and kept off by `settings::keep_to_this_game` |
| Names | The menu, window title and messages say Poka-Poka; the internals keep Yakumo's names (`Yakumo` binary, `Yakumo/PokaPokaG` data folder, `MHP3RD_*` variables, `mhp3rd` namespace) to keep the fork easy to update from Yakumo |

## What changed from the MHP3rd profile

- `host/install/game_identity.hpp`, `executable_preparation.cpp`, `installer.cpp`: ULJM-05881, the unencrypted `MHi2_Master.prx`.
- `host/install/user_data.cpp`: data in `Yakumo/PokaPokaG`, apart from MHP3rd's.
- `host/mhp3rd_profile.{hpp,cpp}`, `host/kernel/kernel.hpp`: 32 MiB, no overlays, the boot path, the null-page setting.
- `host/main.cpp`, `host/camera/*`, `CMakeLists.txt`: MHP3rd camera and aspect hooks never installed.
- `host/hle/*`: the calls listed in the README.
- `src/guest_memory.cpp` (framework): `PSPRECOMP_NULL_PAGE`, off by default.
- `configs/nids.csv`: 76 names for NIDs this game imports.
- `scripts/`, `packaging/linux/`, `packaging/android/`: the build scripts, AppImage packing, the Android app id `io.github.felynevillage.g` and an original icon.
- `host/ui/menu.cpp`, `host/settings/settings.cpp`, `host/input/bindings.cpp`, `host/ui/*`: the menu says Poka-Poka and has Video, Audio, Controls, Mods and System pages. The QoL, Network and Debug pages and the MHP3rd-only rows are gone; fast loading and fast-forward moved to Video; the Mods page switches disc mods (`host/kernel/disc_mods.cpp`); System opens the saves folder instead of importing MHP3rd saves. The save backup reminder and the `--adhoc-server` option are gone.
