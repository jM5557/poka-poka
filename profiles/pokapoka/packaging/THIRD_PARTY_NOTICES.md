# Third-party notices

Poka-Poka is distributed under the MIT License (`Yakumo-LICENSE.txt`, the repository's `LICENSE`, which keeps the copyright notices of the PSPRecomp and Yakumo contributors whose code Poka-Poka is built on). Poka-Poka is a fork of [Yakumo](https://github.com/TeamGDB/Yakumo) that targets only Monster Hunter Diary: Poka Poka Felyne Village G; it is not affiliated with or endorsed by the Yakumo authors.

The programs Poka-Poka builds also contain the third-party software listed here, each under its own license. The license texts are in the same directory as this file: `licenses/` next to the program (`usr/lib/felyne-village-g/licenses/` inside the AppImage) and `assets/licenses/` in the Android APK.

Poka-Poka's source does not include any game assets or original game files. A build, however, contains code recompiled from the player's own copy of the game, so builds are for the player's own use and are not distributed.

## Compiled into the program

### Dear ImGui 1.92.9b

The menu and setup screens. Yakumo, and so Poka-Poka, adds local 64-bit debug texture-ID formatting, checked texture allocation with explicit fatal failure handling, glyph rectangle validation, and font bitmap validation patches. Copyright (c) 2014-2026 Omar Cornut. MIT License: `DearImGui-LICENSE.txt`. <https://github.com/ocornut/imgui>

### tiny-AES-c

AES for PSP save data (Felyne Village G's game module is not encrypted, so the decryption Yakumo uses it for is not needed). Commit `23856752fbd139da0b8ca6e471a13d5bcc99a08d`, released into the public domain under the Unlicense: `tiny-AES-c-UNLICENSE.txt`. <https://github.com/kokke/tiny-AES-c>

### stb_truetype 1.26

Yakumo, and so Poka-Poka, adds independently written bitmap dimension, stride and coordinate checks, SDF padding and SDF/rasterizer allocation-failure handling, and baked-glyph bounds checks to this header and Dear ImGui's `imstb_truetype.h` copy. The original licenses and the distinct upstream ImGui changes are preserved.

Rasterizes the game's text. Copyright (c) 2017 Sean Barrett. Used under the public-domain dedication it offers as an alternative to the MIT License:

> This is free and unencumbered software released into the public domain. Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code form or as a compiled binary, for any purpose, commercial or non-commercial, and by any means.

<https://github.com/nothings/stb>

### stb_image 2.30 and stb_image_write 1.16

Read and write the PNG images of HD texture packs. Yakumo, and so Poka-Poka, adds a checked-allocation guard to stb_image's 16-bit channel conversion. Copyright (c) 2017 Sean Barrett. Used under the same public-domain dedication as stb_truetype above. <https://github.com/nothings/stb>

### xxHash 0.8.3

Hashes textures for HD texture packs. Copyright (c) 2012-2021 Yann Collet. BSD 2-Clause License: `xxHash-LICENSE.txt`. <https://github.com/Cyan4973/xxHash>

## Shipped as separate libraries

These are dynamically linked shared libraries in `lib/` next to the executable (the APK's native libraries on Android). They are unmodified builds of the upstream releases below. You may replace them with your own builds of the same or a compatible version.

### SDL3 3.4.16

Window, input and audio output. Copyright (C) 1997-2026 Sam Lantinga. zlib License: `SDL3-LICENSE.txt`.

- Library: `libSDL3.so.0` (`libSDL3.so` on Android; on a plain Linux build, the system's SDL3 3.2 or newer may be the one packed)
- Source: <https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz> (SHA-256 `7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68`)

### FFmpeg 7.1.5

Decodes the game's ATRAC3 and ATRAC3plus audio and its H.264 movies. FFmpeg is licensed under the GNU Lesser General Public License, version 2.1 or later: `FFmpeg-COPYING.LGPLv2.1.txt`, with the source and configuration also noted in `FFmpeg-SOURCE.txt`. This build contains no GPL or non-free parts. <https://ffmpeg.org/>

- Libraries: `libavcodec.so.61` and `libavutil.so.59`
- Source: <https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz> (SHA-256 `de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f`), not modified
- Configured with:

  ```text
  ./configure --prefix=<prefix> --enable-shared --disable-static --disable-programs --disable-doc --disable-avdevice --disable-avformat --disable-avfilter --disable-swscale --disable-swresample --disable-network --disable-autodetect --disable-everything --enable-decoder=atrac3,atrac3p,h264 --disable-x86asm --disable-debug
  ```

- Built by the Poka-Poka build itself, `profiles/pokapoka/cmake/FFmpeg.cmake` (inherited from Yakumo), which pins the version, checksum and configuration above.
- A build made with `build_linux.sh --ffmpeg system` uses the system's FFmpeg instead and does not pack it; that FFmpeg's own license (for example GPL, for distribution packages built with `--enable-gpl`) then applies to it.

**Source.** Poka-Poka builds are made by each player from their own copy of the game and are not distributed, so there is no release to publish the source beside. The source is the unmodified archive linked above; `FFmpeg-SOURCE.txt` next to the libraries names it.

## Font

### Noto Sans CJK JP (Noto CJK Sans 2.004)

A fallback for Japanese text on systems without a CJK font: `fonts/NotoSansCJKjp-Regular.otf` (`assets/fonts/` in the APK). When the pinned download is unavailable, `build_linux.sh` packs the system's Noto Sans CJK instead, under the same license. Copyright 2014-2021 Adobe (<http://www.adobe.com/>), with Reserved Font Name 'Source'. SIL Open Font License, version 1.1: `NotoSansCJK-OFL.txt`. <https://github.com/notofonts/noto-cjk>
