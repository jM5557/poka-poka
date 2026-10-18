#pragma once

// Identity of the one release this profile supports: Monster Hunter Diary:
// Poka Poka Felyne Village G (Japan, UMD, ULJM-05881, disc version 1.01).
//
// The disc's PSP_GAME/SYSDIR/EBOOT.BIN is only a loader ("mhi_parent_module")
// that starts PSP_GAME/USRDIR/MHi2_Master.prx; that module holds the game and
// is stored unencrypted, so it is what the recompiled code is generated from.
namespace mhp3rd::install {

// DISC_ID in PSP_GAME/PARAM.SFO.
inline constexpr const char *kDiscId = "ULJM05881";
inline constexpr const char *kDiscIdDisplay = "ULJM-05881";
inline constexpr const char *kGameTitle = "Monster Hunter Diary: Poka Poka Felyne Village G";

inline constexpr const char *kExecutablePathOnDisc = "PSP_GAME/USRDIR/MHi2_Master.prx";
inline constexpr const char *kParamSfoPathOnDisc = "PSP_GAME/PARAM.SFO";

// SHA-256 of MHi2_Master.prx as it is on the disc. It is a plain ELF, so the
// executable the recompiled code was generated from is the same file.
inline constexpr const char *kEncryptedExecutableSha256 =
    "3af4813ee99f79d93dded2cad96dba13823d1dce3dd66e8d47318bec6f047416";
inline constexpr const char *kExecutableSha256 = "3af4813ee99f79d93dded2cad96dba13823d1dce3dd66e8d47318bec6f047416";

} // namespace mhp3rd::install
