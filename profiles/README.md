# Profiles

Profiles contain everything PSPRecomp needs for a particular title without putting game-specific addresses or behaviour into the reusable framework. A profile supplies the executable identity and memory map, the native host and HLE modules, build scripts, tests and player documentation.

A profile is selected at configure time:

```bash
cmake -S . -B out/<profile> -DPSPRECOMP_PROFILE=<profile>
```

The repository currently includes [`mhp3rd`](mhp3rd/README.md), which builds Yakumo for **Monster Hunter Portable 3rd HD Ver.** (`NPJB-40001`). It covers the complete recompiled executable and all 355 overlays, Vulkan graphics, audio and movies, saves, input, Yakumo's interface and ad hoc multiplayer.

[`pokapoka`](pokapoka/README.md) is **Poka-Poka**, a port of **Monster Hunter Diary: Poka Poka Felyne Village G** (`ULJM-05881`) forked from that profile. Poka-Poka is built on Yakumo but targets only Felyne Village G, and it is not part of Yakumo or endorsed by its authors; see its [README](pokapoka/README.md) and [bring-up notes](pokapoka/BRINGUP.md). `mhp3rd` is kept unchanged in this repository as the base Poka-Poka was forked from.

See [`docs/PROFILE_GUIDE.md`](../docs/PROFILE_GUIDE.md) before adding another title.
