# Android packaging (Poka-Poka)

What Poka-Poka's Android app is made of besides the native code. `scripts/build_android.sh` builds the native code and packs the APK from these files; the [profile README](../../README.md#android) says how. This packaging is inherited from [Yakumo](https://github.com/TeamGDB/Yakumo) and adapted so that Poka-Poka, which targets only Monster Hunter Diary: Poka Poka Felyne Village G, installs as its own app.

| File | What it is |
| --- | --- |
| `AndroidManifest.xml` | The app: `io.github.felynevillage.g`, named *Felyne Village G*, so it installs beside Yakumo, not over it. Android 10 (API 29) and Vulkan 1.1 required, landscape, the libraries extracted, the backup rules |
| `java/…/YakumoActivity.java` | Yakumo's activity, used unchanged under its own Java package (the host's native code calls it by that name): SDL's activity plus the display cutout, the document picker for folders and files, restarting the app |
| `res/` | The name, the theme without a title bar, the launcher icons (an original paw-print icon made by `packaging/linux/make_icon.py`) and the backup rules (saves and settings only) |
| `build_apk.sh` | Packs and signs an APK without Gradle, with the SDK's own tools |
| `make_icons.sh` | Yakumo's icon script, from its emblem; not used for Poka-Poka's icons |

SDL's own Java classes come from the SDL3 source `scripts/build_android.sh` downloads (`packaging/linux/sources.sh` pins it), not from this directory.
