#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mhp3rd {

// Read-only ISO9660 view of a PSP UMD image. Paths are case-insensitive and
// use '/' separators relative to the image root (e.g. "PSP_GAME/USRDIR/DATA.BIN").
class IsoImage {
public:
    static constexpr std::uint32_t kSectorSize = 2048u;

    struct Entry {
        std::uint32_t lba{};
        std::uint32_t size{};
        bool directory{};
    };

    explicit IsoImage(const std::filesystem::path &path);

    [[nodiscard]] std::optional<Entry> find(std::string path) const;
    [[nodiscard]] std::uint64_t size_bytes() const noexcept { return size_bytes_; }
    // Reads up to output.size() bytes at an absolute image offset.
    std::size_t read(std::uint64_t offset, std::span<std::uint8_t> output);
    [[nodiscard]] std::vector<std::string> list(std::string directory) const;

    // Disc mods (host/kernel/disc_mods.hpp): whole sectors that replace the
    // image's own on every read. rescan() reads the directory again, so a mod
    // that changes it takes effect too.
    void overlay_sector(std::uint32_t sector, std::span<const std::uint8_t> bytes);
    void clear_overlay();
    void rescan();
    [[nodiscard]] std::size_t overlay_sectors() const noexcept { return overlay_.size(); }
    // The image's own bytes, without any overlay.
    std::size_t read_original(std::uint64_t offset, std::span<std::uint8_t> output);

private:
    void scan_directory(std::uint32_t lba, std::uint32_t size, const std::string &prefix, int depth);
    [[nodiscard]] static std::string normalize(std::string path);

    std::ifstream file_;
    std::uint64_t size_bytes_{};
    std::map<std::string, Entry> entries_;
    std::map<std::uint32_t, std::vector<std::uint8_t>> overlay_;
};

} // namespace mhp3rd
