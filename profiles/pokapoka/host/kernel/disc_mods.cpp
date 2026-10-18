#include "kernel/disc_mods.hpp"

#include "platform/utf8_path.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <span>

namespace mhp3rd::disc_mods {
namespace {

constexpr char kMagic[8] = {'P', 'K', 'D', 'M', 'O', 'D', '1', '\0'};
constexpr std::uint32_t kHeaderSize = 128u;
constexpr std::uint32_t kSector = IsoImage::kSectorSize;

std::uint32_t le32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8u) |
        (static_cast<std::uint32_t>(p[2]) << 16u) | (static_cast<std::uint32_t>(p[3]) << 24u);
}

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1u);
}

// A mod name is a plain file stem: no paths.
bool valid_name(const std::string &name) {
    return !name.empty() && name.size() <= 64u && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.';
    }) && name.find("..") == std::string::npos;
}

struct RunState {
    std::filesystem::path directory;
    std::vector<Result> applied;
};
RunState &run_state() {
    static RunState state;
    return state;
}

} // namespace

const std::filesystem::path &directory() {
    return run_state().directory;
}

const std::vector<Result> &applied() {
    return run_state().applied;
}

std::vector<std::string> available(const std::filesystem::path &folder) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".pkdm") continue;
        const std::string name = path_to_utf8(entry.path().stem());
        if (valid_name(name)) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

const char *overridden_by() {
    return std::getenv("MHP3RD_DISC_MODS") != nullptr ? "MHP3RD_DISC_MODS" : nullptr;
}

bool set_enabled(const std::filesystem::path &folder, const std::string &name, bool on, std::string &error) {
    if (!valid_name(name)) {
        error = "not a mod name";
        return false;
    }
    std::vector<std::string> lines;
    bool present = false;
    {
        std::ifstream in(folder / "enabled.txt");
        for (std::string line; std::getline(in, line);) {
            std::string entry = line;
            if (const auto hash = entry.find('#'); hash != std::string::npos) entry.erase(hash);
            if (trim(entry) == name) {
                if (present || !on) continue; // a repeat, or the one being turned off
                present = true;
            }
            lines.push_back(line);
        }
    }
    if (on && !present) lines.push_back(name);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::filesystem::path target = folder / "enabled.txt";
    const std::filesystem::path partial = folder / "enabled.txt.part";
    {
        std::ofstream out(partial, std::ios::trunc);
        for (const std::string &line : lines) out << line << "\n";
        if (!out) {
            error = "cannot write " + path_to_utf8(target);
            return false;
        }
    }
    std::filesystem::rename(partial, target, ec);
    if (ec) {
        error = "cannot write " + path_to_utf8(target) + ": " + ec.message();
        return false;
    }
    return true;
}

std::vector<std::string> enabled(const std::filesystem::path &directory) {
    std::vector<std::string> names;
    const auto add = [&](std::string name) {
        name = trim(std::move(name));
        if (valid_name(name) && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    };
    if (const char *override_list = std::getenv("MHP3RD_DISC_MODS"); override_list != nullptr) {
        const std::string list = override_list;
        if (trim(list) == "none") return {};
        std::size_t start = 0u;
        while (start <= list.size()) {
            const auto comma = list.find(',', start);
            add(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (comma == std::string::npos) break;
            start = comma + 1u;
        }
        return names;
    }
    std::ifstream file(directory / "enabled.txt");
    for (std::string line; std::getline(file, line);) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        add(line);
    }
    return names;
}

Result apply_file(IsoImage &disc, const std::filesystem::path &path) {
    Result result;
    result.name = path_to_utf8(path.stem());
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = "not found";
        return result;
    }
    std::array<std::uint8_t, kHeaderSize> header{};
    file.read(reinterpret_cast<char *>(header.data()), header.size());
    if (file.gcount() != static_cast<std::streamsize>(header.size()) || std::memcmp(header.data(), kMagic, 8u) != 0 ||
        le32(header.data() + 8u) != kHeaderSize) {
        result.error = "not a disc mod file";
        return result;
    }
    const std::uint32_t count = le32(header.data() + 12u);
    const std::uint64_t image_size =
        static_cast<std::uint64_t>(le32(header.data() + 16u)) | (static_cast<std::uint64_t>(le32(header.data() + 20u)) << 32u);
    const std::string expected(reinterpret_cast<const char *>(header.data() + 24u), 64u);
    if (image_size != disc.size_bytes()) {
        result.error = "made for an image of " + std::to_string(image_size) + " bytes, this one has " +
            std::to_string(disc.size_bytes());
        return result;
    }
    if (count == 0u || count > image_size / kSector) {
        result.error = "bad sector count";
        return result;
    }
    std::vector<std::uint32_t> sectors(count);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(count) * kSector);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::array<std::uint8_t, 4> number{};
        file.read(reinterpret_cast<char *>(number.data()), 4);
        file.read(reinterpret_cast<char *>(data.data() + static_cast<std::size_t>(i) * kSector), kSector);
        if (!file) {
            result.error = "truncated";
            return result;
        }
        sectors[i] = le32(number.data());
        if (static_cast<std::uint64_t>(sectors[i]) * kSector + kSector > image_size) {
            result.error = "sector outside the image";
            return result;
        }
    }
    // The original bytes of those sectors must be the ones the mod was made from.
    std::vector<std::uint8_t> original(data.size());
    for (std::uint32_t i = 0; i < count; ++i)
        (void)disc.read_original(static_cast<std::uint64_t>(sectors[i]) * kSector,
            std::span(original.data() + static_cast<std::size_t>(i) * kSector, kSector));
    if (psprecomp::sha256_bytes(original) != expected) {
        result.error = "made for a different disc image (the sectors it replaces do not match)";
        return result;
    }
    for (std::uint32_t i = 0; i < count; ++i)
        disc.overlay_sector(sectors[i], std::span(data.data() + static_cast<std::size_t>(i) * kSector, kSector));
    result.applied = true;
    result.sectors = count;
    return result;
}

std::vector<Result> apply_enabled(IsoImage &disc, const std::filesystem::path &directory) {
    run_state().directory = directory;
    std::vector<Result> results;
    for (const std::string &name : enabled(directory)) {
        Result result = apply_file(disc, directory / (name + ".pkdm"));
        result.name = name;
        if (result.applied)
            std::cout << "[disc-mod] " << name << ": " << result.sectors << " sectors replaced\n";
        else
            std::cerr << "[disc-mod] " << name << " is not applied: " << result.error << "\n";
        results.push_back(std::move(result));
    }
    if (!results.empty()) disc.rescan();
    run_state().applied = results;
    return results;
}

} // namespace mhp3rd::disc_mods
