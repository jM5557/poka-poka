# Poka-Poka

A native port of **Monster Hunter Diary: Poka Poka Felyne Village G** (モンハン日記 ぽかぽかアイルー村G, PSP, Japan, `ULJM-05881`) made by static recompilation: the game's PSP code is translated ahead of time into C++ and compiled for your machine, then run on a reimplementation of the PSP system software. It is not an emulator and not a decompilation.

> [!IMPORTANT]
> **Poka-Poka is built on [Yakumo](https://github.com/TeamGDB/Yakumo)**, TeamGDB's native port of *Monster Hunter Portable 3rd HD Ver.*, and on the [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp) framework it uses. Poka-Poka is a fork that **specifically targets Monster Hunter Diary: Poka Poka Felyne Village G** and no other game: its game profile, `profiles/pokapoka`, adapts Yakumo's recompiler, PSP system and renderer to this title.
>
> Poka-Poka is an independent project. It is not part of Yakumo and is not affiliated with, reviewed or endorsed by TeamGDB or the Yakumo authors. Please do not report Poka-Poka problems to the Yakumo project. To play *Monster Hunter Portable 3rd HD Ver.*, use Yakumo itself.

> **This project does not include any game assets.** You must provide the files from your own legally obtained copy of Monster Hunter Diary: Poka Poka Felyne Village G (`ULJM-05881`, disc version 1.01) to build and play Poka-Poka.

## Legal disclaimer

**Poka-Poka** is an independent, open-source project and is not affiliated with, authorized by, sponsored by, or endorsed by CAPCOM, Sony, or any of their affiliates.

Monster Hunter, Monster Hunter Diary: Poka Poka Felyne Village G (モンハン日記 ぽかぽかアイルー村G), CAPCOM, PlayStation, PSP, and all related trademarks, game assets, artwork, audio, characters, and other intellectual property belong to their respective owners.

**Poka-Poka** does not include any game assets or original game files: no disc image, no copy of the game's executable or data, no code recompiled from it, and no textures, models, audio or video from the game. Every build generates its code from the player's own copy of the game.

Because of that, the programs and packages you build (the executable, the AppImage, the Android APK) contain code derived from your copy of the game. Keep them for your own use and do not share them.

Users are solely responsible for obtaining, dumping, extracting, and using their game copy in accordance with the laws applicable in their jurisdiction. **Poka-Poka** does not support, provide, link to, or encourage the use of unauthorized or pirated copies of the game.

Any references to the original game or its trademarks are made solely for identification, compatibility, and interoperability purposes. The license covering **Poka-Poka** applies only to the project's own original code and materials and to the code it inherits from Yakumo and PSPRecomp under their MIT License; it does not grant any rights to third-party intellectual property.

## Status: playable, early

On Linux (Intel Iris Xe, Mesa) the game shows its autosave notice, the publisher logos and the opening movie, and plays through character creation, the first cutscene and into the village at the PSP's 30 frames per second.

| Works | Not verified yet, or missing |
| --- | --- |
| The whole game module recompiled (22 541 functions) | Saving and loading over a long session |
| Disc reading straight from your `.iso`, menus, text, 2D and 3D drawing | Sound (music, effects and movie audio decode, but nobody has listened yet) |
| Opening movies | The AppImage built in the Steam Runtime container, and the Steam Deck |
| Character creation, cutscenes, the village | The Android build |
| Fan translations as disc mods that can be turned on and off | Online features and local wireless play (not implemented) |

[`profiles/pokapoka/README.md`](profiles/pokapoka/README.md) is the player and builder guide; [`profiles/pokapoka/BRINGUP.md`](profiles/pokapoka/BRINGUP.md) records what was fixed, how it was verified, and what is known to be missing.

## Building and playing

Poka-Poka is built from source, on your machine, from your own disc image. On Arch Linux:

```sh
profiles/pokapoka/scripts/build_linux.sh --deps --iso ~/Games/FelyneVillageG.iso
out/pokapoka/bin/Yakumo profiles/pokapoka/game
```

The first build takes one to three hours. The same script makes an AppImage for Linux desktops and SteamOS (`--appimage`, or `--sdk` to build it in the Steam Runtime container), and `profiles/pokapoka/scripts/build_android.sh` makes an Android APK. Every option is in the [profile README](profiles/pokapoka/README.md).

### English translation

The English fan translation of the game by [mykaadev](https://github.com/mykaadev/MonHun-Nikki-Poka-Poka-Airu-Mura-G-PSP-Translation-Patch) can be used as a *disc mod*: `profiles/pokapoka/tools/make_disc_mod.py` turns its xdelta patch and your clean image into a small mod that is applied while the game reads the disc, leaving your image untouched, and `profiles/pokapoka/scripts/mods.sh` turns it on and off. See [Disc mods](profiles/pokapoka/README.md#disc-mods-fan-translations). The translation is mykaadev's work and is not part of Poka-Poka.

### Names you will still see

To keep the fork easy to update from Yakumo, Poka-Poka keeps Yakumo's internal names: the program is `out/pokapoka/bin/Yakumo` and its window says *Yakumo*, settings and saves live in `~/.local/share/Yakumo/PokaPokaG/`, and the environment variables start with `MHP3RD_`. These names do not mean the program is Yakumo or plays Monster Hunter Portable 3rd.

## How it works

The game's code module, `PSP_GAME/USRDIR/MHi2_Master.prx`, is analyzed and every instruction is emitted as C++, which is compiled into the program; an interpreter covers anything the analysis missed, so nothing stops the game, it only runs slower there. Around that code sits a reimplementation of the PSP system inherited from Yakumo and extended for this game: a kernel with threads, semaphores, lightweight mutexes, callbacks and timers; disc I/O read straight from the image, including the raw UMD block device this game uses; a Vulkan renderer for the PSP's graphics engine; software voice mixing, streamed ATRAC music and H.264 movies; and input from SDL3.

## Repository layout

```text
profiles/pokapoka/   Poka-Poka: everything specific to Felyne Village G, with its
                     host, build scripts, packaging, tests and documentation
include/psprecomp/   Framework interfaces: runtime, memory, Allegrex state (from PSPRecomp, via Yakumo)
src/                 Framework: ELF loading, decoder, runtime, interpreter
tools/               Framework: analyzer and C++ code generator
tests/               Framework regression tests
configs/             PSP NID data
profiles/mhp3rd/     Yakumo's original profile for Monster Hunter Portable 3rd HD Ver.,
                     kept unchanged as the base Poka-Poka was forked from
docs/                Yakumo's guides, inherited (see below)
```

The recompiled code is generated locally from your copy of the game and is never committed.

## Documentation

| Document | What it covers |
| --- | --- |
| [`profiles/pokapoka/README.md`](profiles/pokapoka/README.md) | Building on Arch Linux, the AppImage, Android, disc mods, running and debugging Poka-Poka |
| [`profiles/pokapoka/BRINGUP.md`](profiles/pokapoka/BRINGUP.md) | What was fixed to make the game run, how each fix was verified, known gaps |
| [`profiles/pokapoka/MHP3RD_HOST_REFERENCE.md`](profiles/pokapoka/MHP3RD_HOST_REFERENCE.md) | Yakumo's guide to the controls, settings and variables Poka-Poka inherits |
| [`docs/`](docs/), `AGENTS.md`, `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md` | Yakumo's own documents, inherited unchanged apart from a note at the top. They describe Yakumo and Monster Hunter Portable 3rd HD Ver.; the framework parts (architecture, build system, profile guide) apply to Poka-Poka too |
| `README.es.md`, `README.ru.md` | Yakumo's Spanish and Russian READMEs, inherited; they describe Yakumo |

## Credits

Poka-Poka exists because of the work it is built on:

- [Yakumo](https://github.com/TeamGDB/Yakumo) by [@MHunterG](https://github.com/MHunterG) and [@mojitosunrise](https://github.com/mojitosunrise) (TeamGDB): the PSP system, renderer, audio, movies, saves, interface and build that Poka-Poka adapts
- [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp): the static recompilation framework
- [mykaadev's English translation patch](https://github.com/mykaadev/MonHun-Nikki-Poka-Poka-Airu-Mura-G-PSP-Translation-Patch), usable as a disc mod
- [SDL3](https://www.libsdl.org/): windowing, input and audio output
- [FFmpeg](https://ffmpeg.org/): music and movie decoding
- [Vulkan](https://www.vulkan.org/): rendering
- [Dear ImGui](https://github.com/ocornut/imgui): the menus and setup screens
- [tiny-AES-c](https://github.com/kokke/tiny-AES-c): save-data support
- [stb_truetype](https://github.com/nothings/stb): font rasterization
- [xxHash](https://github.com/Cyan4973/xxHash): texture hashing

Third-party notices are in [`profiles/pokapoka/packaging/THIRD_PARTY_NOTICES.md`](profiles/pokapoka/packaging/THIRD_PARTY_NOTICES.md).

## License

MIT; see [`LICENSE`](LICENSE). The copyright notices of the PSPRecomp and Yakumo contributors in it are kept, as the license requires. Third-party files keep their own notices beside them.
