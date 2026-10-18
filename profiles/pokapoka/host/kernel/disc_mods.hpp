#pragma once
// Disc mods: whole-sector replacements for the disc image, such as a fan
// translation made as an xdelta patch against the clean image
// (tools/make_disc_mod.py turns one into a mod). The image itself is never
// changed; the replaced sectors are overlaid on every read, so a mod can be
// turned on and off between runs.
//
// Mods are <name>.pkdm files in the data directory's disc_mods/ folder. The
// ones listed in disc_mods/enabled.txt (one name a line, # for comments) are
// applied in order; MHP3RD_DISC_MODS=name[,name...] overrides that list for
// one run, and MHP3RD_DISC_MODS=none turns them all off.
//
// File format, little-endian: "PKDMOD1\0", u32 header size (128), u32 sector
// count, u64 image size, 64 hex digits of the SHA-256 of the original bytes of
// every replaced sector in file order, zero padding to the header size; then
// per sector a u32 sector number and its 2048 new bytes. A mod whose original
// bytes do not match the image (another release or a modified image) is
// refused.
#include "kernel/iso_image.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace mhp3rd::disc_mods {

struct Result {
    std::string name;
    bool applied{};
    std::size_t sectors{};
    std::string error; // why it was not applied
};

// The mods that should be on: MHP3RD_DISC_MODS, else enabled.txt.
[[nodiscard]] std::vector<std::string> enabled(const std::filesystem::path &directory);

// Loads one mod file and overlays it on `disc`, after checking it fits.
Result apply_file(IsoImage &disc, const std::filesystem::path &file);

// Applies every enabled mod in `directory` and logs the outcome.
std::vector<Result> apply_enabled(IsoImage &disc, const std::filesystem::path &directory);

// For the menu (ui/menu.cpp, Mods). The folder this run looked in (empty
// before a disc was attached) and what it applied.
[[nodiscard]] const std::filesystem::path &directory();
[[nodiscard]] const std::vector<Result> &applied();
// The mods in `directory`: every <name>.pkdm, by name.
[[nodiscard]] std::vector<std::string> available(const std::filesystem::path &directory);
// MHP3RD_DISC_MODS, when it decides this run's mods instead of enabled.txt.
[[nodiscard]] const char *overridden_by();
// Turns a mod on or off in enabled.txt, keeping the order of the others and
// any comments. It takes effect the next time the game starts. False, with
// `error`, when the file cannot be written.
bool set_enabled(const std::filesystem::path &directory, const std::string &name, bool on, std::string &error);

} // namespace mhp3rd::disc_mods
