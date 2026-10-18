// IoFileMgrForUser and sceUmdUser: UMD access straight from the disc image
// (including raw "sce_lbn" sector files) and a host directory for ms0:.
#include "hle_common.hpp"
#include "utility_dialog.hpp"
#include "kernel/fast_loading.hpp"
#include "kernel/load_trace.hpp"
#include "kernel/iso_image.hpp"
#include "kernel/disc_mods.hpp"
#include "mods/mhp3rd_mods.hpp"
#include "text/translation.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

namespace mhp3rd {
namespace {

namespace io_error {
constexpr std::uint32_t kFileNotFound = 0x80010002u;
constexpr std::uint32_t kBadFileDescriptor = 0x80010009u;
constexpr std::uint32_t kDeviceNotFound = 0x80010013u;
constexpr std::uint32_t kInvalidArgument = 0x80010016u;
constexpr std::uint32_t kReadOnly = 0x8001001Eu;
} // namespace io_error

constexpr std::uint32_t kOpenWrite = 0x0002u;
constexpr std::uint32_t kOpenAppend = 0x0100u;
constexpr std::uint32_t kOpenCreate = 0x0200u;
constexpr std::uint32_t kOpenTruncate = 0x0400u;

enum class Device { Disc, MemoryStick, Unknown };

struct OpenFile {
    enum class Kind { Disc, Host, Directory } kind{};
    std::string path;
    std::uint64_t disc_offset{}; // absolute image offset of byte 0
    std::uint64_t size{};
    std::uint64_t position{};
    std::unique_ptr<std::fstream> host;
    // Inside DATA.BIN: read through the mods, which may change what the game
    // gets there (host/mods/mhp3rd_mods.hpp). Byte 0 is this far into it.
    bool archive{};
    std::uint64_t archive_offset{};
    // Opened through umd0:/umd1:: the game seeks and reads in sectors. The
    // position stays in bytes; only the guest's numbers are converted.
    bool sectors{};
};

struct IoState {
    std::unique_ptr<IsoImage> disc;
    std::filesystem::path memory_stick;
    std::map<std::uint32_t, OpenFile> files;
    std::uint32_t next_fd{3u};
};

IoState &io() {
    static IoState state;
    return state;
}

struct SplitPath {
    Device device{Device::Unknown};
    // umd0: and umd1: are block devices: opened as the device itself or as a
    // sce_lbn sector range, offsets and sizes count 2048-byte sectors. Named
    // files on them, and everything on disc0: and isofs0:, count bytes.
    bool block_device{};
    std::string path; // without device, '/' separated, no leading '/'
};

SplitPath split_path(const std::string &full) {
    SplitPath result;
    const auto colon = full.find(':');
    std::string device = colon == std::string::npos ? "disc0" : full.substr(0, colon);
    std::transform(device.begin(), device.end(), device.begin(), [](unsigned char c) { return std::tolower(c); });
    if (device == "disc0" || device == "umd0" || device == "umd1" || device == "isofs0")
        result.device = Device::Disc;
    else if (device == "ms0" || device == "fatms0")
        result.device = Device::MemoryStick;
    result.block_device = device == "umd0" || device == "umd1";

    std::string rest = colon == std::string::npos ? full : full.substr(colon + 1u);
    std::replace(rest.begin(), rest.end(), '\\', '/');
    std::vector<std::string> parts;
    std::size_t start = 0u;
    while (start <= rest.size()) {
        const auto end = rest.find('/', start);
        std::string part = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (end == std::string::npos) break;
        start = end + 1u;
    }
    for (std::size_t i = 0; i < parts.size(); ++i) result.path += (i == 0 ? "" : "/") + parts[i];
    return result;
}

// "sce_lbn0x5e0_size0x1000" -> {lba, size}
std::optional<std::pair<std::uint64_t, std::uint64_t>> parse_lbn_path(const std::string &path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto lbn = lower.find("sce_lbn");
    const auto size = lower.find("_size");
    if (lbn != 0u || size == std::string::npos) return std::nullopt;
    const auto parse = [](const std::string &text) -> std::optional<std::uint64_t> {
        std::string digits = text;
        int base = 10;
        if (digits.starts_with("0x")) {
            digits = digits.substr(2);
            base = 16;
        }
        std::uint64_t value{};
        const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
        if (ec != std::errc{} || ptr != digits.data() + digits.size()) return std::nullopt;
        return value;
    };
    const auto lba = parse(lower.substr(7, size - 7));
    const auto length = parse(lower.substr(size + 5));
    if (!lba || !length) return std::nullopt;
    return std::make_pair(*lba, *length);
}

std::filesystem::path host_path(const std::string &path) {
    return io().memory_stick / path;
}

bool trace_io() {
    static const bool enabled = std::getenv("MHP3RD_TRACE_IO") != nullptr;
    return enabled;
}

std::int64_t open_file(const std::string &full_path, std::uint32_t flags) {
    const SplitPath split = split_path(full_path);
    OpenFile file;
    file.path = full_path;
    if (split.device == Device::Disc) {
        if (!io().disc) return static_cast<std::int32_t>(io_error::kDeviceNotFound);
        if ((flags & kOpenWrite) != 0u) return static_cast<std::int32_t>(io_error::kReadOnly);
        if (const auto lbn = parse_lbn_path(split.path)) {
            file.kind = OpenFile::Kind::Disc;
            file.sectors = split.block_device; // umd1:/sce_lbn...: sectors; disc0:/sce_lbn...: bytes
            file.disc_offset = lbn->first * IsoImage::kSectorSize;
            file.size = lbn->second;
        } else if (split.path.empty()) {
            // Opening the device itself gives raw sector access. The ISO root
            // also has an entry, so resolve the device before that directory.
            file.kind = OpenFile::Kind::Disc;
            file.size = io().disc->size_bytes();
            file.sectors = split.block_device; // umd0:/umd1: itself is a block device
        } else if (const auto entry = io().disc->find(split.path)) {
            file.kind = entry->directory ? OpenFile::Kind::Directory : OpenFile::Kind::Disc;
            file.disc_offset = static_cast<std::uint64_t>(entry->lba) * IsoImage::kSectorSize;
            file.size = entry->size;
        } else {
            return static_cast<std::int32_t>(io_error::kFileNotFound);
        }
        // DATA.BIN, or raw sectors inside it, as the game loads a module stored there.
        if (const auto data_bin = mods::data_bin_on_disc(); data_bin && !split.path.empty() &&
            file.disc_offset >= data_bin->offset && file.disc_offset < data_bin->offset + data_bin->size) {
            file.archive = true;
            file.archive_offset = file.disc_offset - data_bin->offset;
            if (file.archive_offset == 0u && file.size == data_bin->size) file.size = mods::data_bin_size();
        }
    } else if (split.device == Device::MemoryStick) {
        const auto path = host_path(split.path);
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (!exists && (flags & kOpenCreate) == 0u) return static_cast<std::int32_t>(io_error::kFileNotFound);
        std::ios::openmode mode = std::ios::binary | std::ios::in;
        if ((flags & kOpenWrite) != 0u) {
            std::filesystem::create_directories(path.parent_path(), ec);
            mode |= std::ios::out;
            if (!exists || (flags & kOpenTruncate) != 0u) mode |= std::ios::trunc;
            if ((flags & kOpenAppend) != 0u) mode |= std::ios::app;
        }
        file.kind = OpenFile::Kind::Host;
        file.host = std::make_unique<std::fstream>(path, mode);
        if (!*file.host) return static_cast<std::int32_t>(io_error::kFileNotFound);
        file.size = std::filesystem::exists(path, ec) ? std::filesystem::file_size(path, ec) : 0u;
    } else {
        return static_cast<std::int32_t>(io_error::kDeviceNotFound);
    }
    const std::uint32_t fd = io().next_fd++;
    io().files.emplace(fd, std::move(file));
    return fd;
}

void write_date_time(psprecomp::GuestMemory &memory, std::uint32_t address, std::time_t time) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    memory.store16(address, static_cast<std::uint16_t>(tm.tm_year + 1900));
    memory.store16(address + 2u, static_cast<std::uint16_t>(tm.tm_mon + 1));
    memory.store16(address + 4u, static_cast<std::uint16_t>(tm.tm_mday));
    memory.store16(address + 6u, static_cast<std::uint16_t>(tm.tm_hour));
    memory.store16(address + 8u, static_cast<std::uint16_t>(tm.tm_min));
    memory.store16(address + 10u, static_cast<std::uint16_t>(tm.tm_sec));
    memory.store32(address + 12u, 0u);
}

// SceIoStat: mode, attr, size(64), ctime, atime, mtime, private[6].
void write_stat(
    psprecomp::GuestMemory &memory, std::uint32_t address, bool directory, std::uint64_t size, std::uint32_t lba) {
    for (std::uint32_t i = 0; i < 88u; ++i) memory.store8(address + i, 0u);
    memory.store32(address, (directory ? 0x1000u : 0x2000u) | 0x1FFu);
    memory.store32(address + 4u, (directory ? 0x10u : 0x20u) | 0x7u);
    store64(memory, address + 8u, size);
    const std::time_t now = std::time(nullptr);
    write_date_time(memory, address + 16u, now);
    write_date_time(memory, address + 32u, now);
    write_date_time(memory, address + 48u, now);
    memory.store32(address + 64u, lba);
}

// Reads a disc file at `offset` into it.
std::size_t read_disc(const OpenFile &file, std::uint64_t offset, std::span<std::uint8_t> out) {
    if (file.archive && mods::serving()) return mods::read_data_bin(file.archive_offset + offset, out);
    return io().disc->read(file.disc_offset + offset, out);
}

// sceIoRead and sceIoReadAsync: bytes read, or an error code.
std::int64_t do_read(Runtime &rt, std::uint32_t fd, std::uint32_t address, std::uint32_t requested) {
        auto found = io().files.find(fd);
        if (found == io().files.end() || found->second.kind == OpenFile::Kind::Directory)
            return static_cast<std::int32_t>(io_error::kBadFileDescriptor);
        OpenFile &file = found->second;
        std::vector<std::uint8_t> buffer;
        const std::uint64_t unit = file.sectors ? IsoImage::kSectorSize : 1u;
        if (file.sectors) requested = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                              static_cast<std::uint64_t>(requested) * unit, 0xFFFFFFFFu));
        if (file.kind == OpenFile::Kind::Disc) {
            const std::uint64_t available = file.position < file.size ? file.size - file.position : 0u;
            requested = static_cast<std::uint32_t>(std::min<std::uint64_t>(requested, available));
            buffer.resize(requested);
            const std::size_t count = read_disc(file, file.position, buffer);
            buffer.resize(count);
        } else {
            buffer.resize(requested);
            file.host->clear();
            file.host->seekg(static_cast<std::streamoff>(file.position));
            file.host->read(reinterpret_cast<char *>(buffer.data()), requested);
            buffer.resize(static_cast<std::size_t>(file.host->gcount()));
        }
        rt.memory().copy_in(address, buffer);
        // A disc read is what tells a load from play (kernel/fast_loading.hpp).
        if (file.kind == OpenFile::Kind::Disc) {
            load_trace::note_disc_read(buffer.size());
            fast_loading::note_disc_read();
            // A read inside DATA.BIN may carry a text block the translation
            // replaces, in place, before the game copies it out
            // (text/translation.hpp).
            if (file.archive) text::translate_read(file.archive_offset + file.position, buffer);
        } else {
            load_trace::note_memory_stick_read(buffer.size());
        }
        if (trace_io())
            std::cerr << "[io] read fd=" << fd << " " << file.path << " offset=" << file.position
                      << " size=" << buffer.size() << " -> " << psprecomp::hex32(address) << "\n";
        file.position += buffer.size();
        return static_cast<std::int64_t>(buffer.size() / unit);
}

// sceIoLseek and sceIoLseekAsync: the new position, or an error code.
std::int64_t do_lseek(std::uint32_t fd, std::uint64_t raw_offset, std::uint32_t whence) {
    const auto bad = [](std::uint32_t error) { return static_cast<std::int64_t>(static_cast<std::int32_t>(error)); };
    auto found = io().files.find(fd);
    if (found == io().files.end()) return bad(io_error::kBadFileDescriptor);
    OpenFile &file = found->second;
    const std::int64_t unit = file.sectors ? static_cast<std::int64_t>(IsoImage::kSectorSize) : 1;
    const auto offset = static_cast<std::int64_t>(raw_offset) * unit;
    std::int64_t base = 0;
    switch (whence) {
    case 0u:
        base = 0;
        break;
    case 1u:
        base = static_cast<std::int64_t>(file.position);
        break;
    case 2u:
        base = static_cast<std::int64_t>(file.size);
        break;
    default:
        return bad(io_error::kInvalidArgument);
    }
    const std::int64_t target = base + offset;
    if (target < 0) return bad(io_error::kInvalidArgument);
    file.position = static_cast<std::uint64_t>(target);
    if (trace_io())
        std::cerr << "[io] lseek fd=" << fd << " " << file.path << " -> " << file.position
                  << (file.sectors ? " bytes (sector " + std::to_string(target / unit) + ")" : std::string()) << "\n";
    return target / unit;
}

// Asynchronous I/O. The host completes each operation as it is issued, so the
// result is ready by the time the game polls or waits for it; one result is
// kept per file descriptor, as the PSP keeps one operation in flight per file.
struct AsyncResult {
    std::int64_t value{};
    bool pending{};
    bool close_on_wait{};
};
std::map<std::uint32_t, AsyncResult> &async_results() {
    static std::map<std::uint32_t, AsyncResult> results;
    return results;
}
constexpr std::uint32_t kNoAsync = 0x8002032Au; // SCE_KERNEL_ERROR_NOASYNC

void set_async(std::uint32_t fd, std::int64_t value, bool close_on_wait = false) {
    async_results()[fd] = AsyncResult{value, true, close_on_wait};
    if (trace_io()) std::cerr << "[io] async fd=" << fd << " -> " << value << "\n";
}

// Hands the result of `fd`'s operation to the game; 0, or an error.
std::uint32_t take_async(psprecomp::GuestMemory &memory, std::uint32_t fd, std::uint32_t result_address) {
    auto found = async_results().find(fd);
    if (found == async_results().end() || !found->second.pending)
        return io().files.contains(fd) ? kNoAsync : io_error::kBadFileDescriptor;
    if (result_address != 0u) store64(memory, result_address, static_cast<std::uint64_t>(found->second.value));
    const bool close = found->second.close_on_wait;
    async_results().erase(found);
    if (close) io().files.erase(fd);
    return 0u;
}


// sceUtilityGamedataInstall: copies the disc's PSP_GAME/INSDIR to the memory
// stick, which the game then reads instead of the disc
// (ms0:/PSP/SAVEDATA/ULJM05881DATAINSTALL/POKAPOKA.DBN in MHi2_Master.prx).
// The folder name is the game's; the parameter block is not parsed yet.
constexpr const char *kInstallSource = "PSP_GAME/INSDIR";
constexpr const char *kInstallFolder = "PSP/SAVEDATA/ULJM05881DATAINSTALL";

bool install_game_data() {
    if (!io().disc) return false;
    std::error_code ec;
    const std::filesystem::path folder = io().memory_stick / kInstallFolder;
    std::filesystem::create_directories(folder, ec);
    if (ec) return false;
    for (const std::string &name : io().disc->list(kInstallSource)) {
        const auto entry = io().disc->find(std::string(kInstallSource) + "/" + name);
        if (!entry || entry->directory) continue;
        const std::filesystem::path target = folder / name;
        if (std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) == entry->size) continue;
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        std::vector<std::uint8_t> chunk(1u << 20u);
        std::uint64_t done = 0u;
        while (done < entry->size && out) {
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), entry->size - done));
            const std::size_t got =
                io().disc->read(static_cast<std::uint64_t>(entry->lba) * 2048u + done, std::span(chunk.data(), want));
            if (got == 0u) break;
            out.write(reinterpret_cast<const char *>(chunk.data()), static_cast<std::streamsize>(got));
            done += got;
        }
        std::cout << "[install] " << name << ": " << done << " of " << entry->size << " bytes\n";
        if (done != entry->size) return false;
    }
    return true;
}

} // namespace

std::size_t read_open_file(std::uint32_t fd, std::uint64_t offset, std::uint8_t *output, std::size_t size) {
    const auto found = io().files.find(fd);
    if (found == io().files.end()) return 0u;
    OpenFile &file = found->second;
    const std::span<std::uint8_t> out(output, size);
    if (file.kind == OpenFile::Kind::Disc) {
        if (!io().disc || offset >= file.size) return 0u;
        return read_disc(file, offset, out.first(std::min<std::uint64_t>(size, file.size - offset)));
    }
    if (file.kind == OpenFile::Kind::Host) {
        file.host->clear();
        file.host->seekg(static_cast<std::streamoff>(offset));
        file.host->read(reinterpret_cast<char *>(output), static_cast<std::streamsize>(size));
        const auto count = static_cast<std::size_t>(file.host->gcount());
        file.host->clear();
        return count;
    }
    return 0u;
}

void register_io(
    HleRegistrar &hle, const std::filesystem::path &disc_image, const std::filesystem::path &memory_stick) {
    if (!disc_image.empty()) {
        io().disc = std::make_unique<IsoImage>(disc_image);
        // Disc mods (kernel/disc_mods.hpp) live beside the memory stick.
        (void)disc_mods::apply_enabled(*io().disc, memory_stick.parent_path() / "disc_mods");
    }
    io().memory_stick = memory_stick;
    mods::attach_disc(io().disc.get());

    hle.add("IoFileMgrForUser", "sceIoOpen", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string path = read_cstring(rt.memory(), arg(ctx, 0), 256u);
        const std::int64_t fd = open_file(path, arg(ctx, 1));
        if (trace_io() || fd < 0)
            std::cerr << "[io] open " << path << " flags=" << psprecomp::hex32(arg(ctx, 1)) << " -> "
                      << psprecomp::hex32(static_cast<std::uint32_t>(fd)) << "\n";
        kernel().finish(ctx, static_cast<std::uint32_t>(fd));
    });
    hle.add("IoFileMgrForUser", "sceIoClose", [](Runtime &, AllegrexContext &ctx) {
        kernel().finish(ctx, io().files.erase(arg(ctx, 0)) != 0u ? 0u : io_error::kBadFileDescriptor);
    });
    hle.add("IoFileMgrForUser", "sceIoRead", [](Runtime &rt, AllegrexContext &ctx) {
        kernel().finish(ctx, static_cast<std::uint32_t>(do_read(rt, arg(ctx, 0), arg(ctx, 1), arg(ctx, 2))));
    });
    hle.add("IoFileMgrForUser", "sceIoWrite", [](Runtime &rt, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        const std::uint32_t address = arg(ctx, 1);
        const std::uint32_t size = arg(ctx, 2);
        if (fd == 1u || fd == 2u) {
            std::string text(size, '\0');
            for (std::uint32_t i = 0; i < size; ++i) text[i] = static_cast<char>(rt.memory().load8(address + i));
            std::cerr << "[guest] " << text;
            kernel().finish(ctx, size);
            return;
        }
        auto found = io().files.find(fd);
        if (found == io().files.end() || found->second.kind != OpenFile::Kind::Host) {
            kernel().finish(ctx, io_error::kBadFileDescriptor);
            return;
        }
        std::vector<char> data(size);
        for (std::uint32_t i = 0; i < size; ++i) data[i] = static_cast<char>(rt.memory().load8(address + i));
        OpenFile &file = found->second;
        file.host->clear();
        file.host->seekp(static_cast<std::streamoff>(file.position));
        file.host->write(data.data(), size);
        file.host->flush();
        file.position += size;
        file.size = std::max(file.size, file.position);
        kernel().finish(ctx, size);
    });
    hle.add("IoFileMgrForUser", "sceIoLseek", [](Runtime &, AllegrexContext &ctx) {
        kernel().finish64(ctx, static_cast<std::uint64_t>(do_lseek(arg(ctx, 0), arg64(ctx, 2), arg(ctx, 4))));
    });
    hle.add("IoFileMgrForUser", "sceIoGetstat", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string path = read_cstring(rt.memory(), arg(ctx, 0), 256u);
        const SplitPath split = split_path(path);
        std::uint32_t result = io_error::kFileNotFound;
        if (split.device == Device::Disc && io().disc) {
            if (const auto entry = io().disc->find(split.path)) {
                // DATA.BIN is as large as the mods make it.
                const auto data_bin = mods::data_bin_on_disc();
                const bool is_data_bin = data_bin && !entry->directory &&
                    static_cast<std::uint64_t>(entry->lba) * IsoImage::kSectorSize == data_bin->offset;
                write_stat(rt.memory(), arg(ctx, 1), entry->directory,
                    is_data_bin ? mods::data_bin_size() : entry->size, entry->lba);
                result = 0u;
            }
        } else if (split.device == Device::MemoryStick) {
            std::error_code ec;
            const auto host = host_path(split.path);
            if (std::filesystem::exists(host, ec)) {
                const bool directory = std::filesystem::is_directory(host, ec);
                write_stat(
                    rt.memory(), arg(ctx, 1), directory, directory ? 0u : std::filesystem::file_size(host, ec), 0u);
                result = 0u;
            }
        }
        if (trace_io() || result != 0u)
            std::cerr << "[io] getstat " << path << " -> " << psprecomp::hex32(result) << "\n";
        kernel().finish(ctx, result);
    });
    hle.add("IoFileMgrForUser", "sceIoDopen", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string path = read_cstring(rt.memory(), arg(ctx, 0), 256u);
        const SplitPath split = split_path(path);
        bool exists = false;
        if (split.device == Device::Disc && io().disc) {
            const auto entry = io().disc->find(split.path);
            exists = split.path.empty() || (entry && entry->directory);
        } else if (split.device == Device::MemoryStick) {
            std::error_code ec;
            exists = std::filesystem::is_directory(host_path(split.path), ec);
        }
        if (!exists) {
            std::cerr << "[io] dopen " << path << " -> not found\n";
            kernel().finish(ctx, io_error::kFileNotFound);
            return;
        }
        OpenFile file;
        file.kind = OpenFile::Kind::Directory;
        file.path = path;
        const std::uint32_t fd = io().next_fd++;
        io().files.emplace(fd, std::move(file));
        kernel().finish(ctx, fd);
    });
    hle.add("IoFileMgrForUser", "sceIoDclose", [](Runtime &, AllegrexContext &ctx) {
        kernel().finish(ctx, io().files.erase(arg(ctx, 0)) != 0u ? 0u : io_error::kBadFileDescriptor);
    });
    hle.add("IoFileMgrForUser", "sceIoRename", [](Runtime &rt, AllegrexContext &ctx) {
        const SplitPath from = split_path(read_cstring(rt.memory(), arg(ctx, 0), 256u));
        const SplitPath to = split_path(read_cstring(rt.memory(), arg(ctx, 1), 256u));
        if (from.device != Device::MemoryStick || to.device != Device::MemoryStick) {
            kernel().finish(ctx, io_error::kReadOnly);
            return;
        }
        std::error_code ec;
        std::filesystem::rename(host_path(from.path), host_path(to.path), ec);
        kernel().finish(ctx, ec ? io_error::kFileNotFound : 0u);
    });
    hle.add("IoFileMgrForUser", "sceIoDevctl", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string device = read_cstring(rt.memory(), arg(ctx, 0), 64u);
        const std::uint32_t command = arg(ctx, 1);
        const std::uint32_t input = arg(ctx, 2);
        const std::uint32_t output = arg(ctx, 4);
        const std::uint32_t output_length = arg(ctx, 5);
        auto &memory = rt.memory();
        switch (command) {
        case 0x02015804u:   // register memory stick insert/eject callback (ms0:)
        case 0x02415821u: { // the same for fatms0:
            // The game waits for the first notification before it continues, and
            // refuses to save while it has not been told a card is inserted, so
            // report the card as present right away.
            const std::uint32_t callback = input != 0u ? memory.load32(input) : 0u;
            if (callback != 0u) kernel().notify_callback(static_cast<SceUID>(callback), 1u);
            break;
        }
        case 0x02015805u: // unregister
        case 0x02415822u:
            break;
        case 0x02025801u: // memory stick state: 4 = inserted and ready
        case 0x02025806u: // memory stick inserted
            if (output != 0u && output_length >= 4u) memory.store32(output, command == 0x02025801u ? 4u : 1u);
            break;
        case 0x02425823u: // fatms inserted
            if (output != 0u && output_length >= 4u) memory.store32(output, 1u);
            break;
        case 0x02425818u: { // free space: input holds a pointer to the info block
            const std::uint32_t info = input != 0u ? memory.load32(input) : 0u;
            if (info != 0u) {
                constexpr std::uint32_t kSectorSize = 0x200u;
                constexpr std::uint32_t kSectorsPerCluster = 0x20u;
                constexpr std::uint32_t kFreeClusters = 0x100000u / 0x10u; // ~1 GiB free
                memory.store32(info, kFreeClusters * 2u);
                memory.store32(info + 4u, kFreeClusters);
                memory.store32(info + 8u, kFreeClusters);
                memory.store32(info + 12u, kSectorSize);
                memory.store32(info + 16u, kSectorsPerCluster);
            }
            break;
        }
        default:
            log_once("devctl:" + device + psprecomp::hex32(command),
                "[io] devctl " + device + " cmd=" + psprecomp::hex32(command) + " (unhandled, returning 0)");
            break;
        }
        kernel().finish(ctx, 0u);
    });


    hle.add("sceUmdUser", "sceUmdGetDriveStat", [](Runtime &, AllegrexContext &ctx) {
        constexpr std::uint32_t kPresent = 0x02u;
        constexpr std::uint32_t kReady = 0x10u;
        constexpr std::uint32_t kReadable = 0x20u;
        kernel().finish(ctx, io().disc ? (kPresent | kReady | kReadable) : 0x01u);
    });
    hle.add("sceUmdUser", "sceUmdGetErrorStat", [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, 0u); });

    // Asynchronous file I/O (completed at once; see AsyncResult).
    hle.add("IoFileMgrForUser", "sceIoOpenAsync", [](Runtime &rt, AllegrexContext &ctx) {
        const std::string path = read_cstring(rt.memory(), arg(ctx, 0), 256u);
        const std::int64_t fd = open_file(path, arg(ctx, 1));
        if (trace_io() || fd < 0)
            std::cerr << "[io] open-async " << path << " -> " << psprecomp::hex32(static_cast<std::uint32_t>(fd)) << "\n";
        if (fd >= 0) set_async(static_cast<std::uint32_t>(fd), fd);
        kernel().finish(ctx, static_cast<std::uint32_t>(fd));
    });
    hle.add("IoFileMgrForUser", "sceIoReadAsync", [](Runtime &rt, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        if (!io().files.contains(fd)) {
            kernel().finish(ctx, io_error::kBadFileDescriptor);
            return;
        }
        set_async(fd, do_read(rt, fd, arg(ctx, 1), arg(ctx, 2)));
        kernel().finish(ctx, 0u);
    });
    hle.add("IoFileMgrForUser", "sceIoWriteAsync", [](Runtime &, AllegrexContext &ctx) {
        log_once("io-write-async", "[io] sceIoWriteAsync is not implemented");
        kernel().finish(ctx, io_error::kBadFileDescriptor);
    });
    hle.add("IoFileMgrForUser", "sceIoLseekAsync", [](Runtime &, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        if (!io().files.contains(fd)) {
            kernel().finish(ctx, io_error::kBadFileDescriptor);
            return;
        }
        set_async(fd, do_lseek(fd, arg64(ctx, 2), arg(ctx, 4)));
        kernel().finish(ctx, 0u);
    });
    hle.add("IoFileMgrForUser", "sceIoCloseAsync", [](Runtime &, AllegrexContext &ctx) {
        const std::uint32_t fd = arg(ctx, 0);
        if (!io().files.contains(fd)) {
            kernel().finish(ctx, io_error::kBadFileDescriptor);
            return;
        }
        set_async(fd, 0, true);
        kernel().finish(ctx, 0u);
    });
    const auto wait_async = [](Runtime &rt, AllegrexContext &ctx) {
        kernel().finish(ctx, take_async(rt.memory(), arg(ctx, 0), arg(ctx, 1)));
    };
    hle.try_add("IoFileMgrForUser", "sceIoWaitAsync", wait_async);
    hle.add("IoFileMgrForUser", "sceIoWaitAsyncCB", [wait_async](Runtime &rt, AllegrexContext &ctx) {
        (void)kernel().deliver_callbacks();
        wait_async(rt, ctx);
    });
    // 0: done (the result is written); 1 would mean still busy, which never happens here.
    hle.add("IoFileMgrForUser", "sceIoPollAsync", wait_async);
    hle.add("IoFileMgrForUser", "sceIoIoctl", [](Runtime &, AllegrexContext &ctx) {
        log_once("ioctl-" + psprecomp::hex32(arg(ctx, 1)),
            "[io] sceIoIoctl fd=" + std::to_string(arg(ctx, 0)) + " cmd=" + psprecomp::hex32(arg(ctx, 1)) +
                " is not implemented; returning 0");
        kernel().finish(ctx, 0u);
    });

    hle.add("sceUmdUser", "sceUmdCheckMedium",
        [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, io().disc ? 1u : 0u); });
    // The disc is always ready, so waiting for a drive state returns at once.
    const auto umd_wait = [](Runtime &, AllegrexContext &ctx) {
        (void)kernel().deliver_callbacks();
        kernel().finish(ctx, 0u);
    };
    hle.try_add("sceUmdUser", "sceUmdWaitDriveStat", umd_wait);
    hle.add("sceUmdUser", "sceUmdWaitDriveStatCB", umd_wait);
    hle.add("sceUmdUser", "sceUmdWaitDriveStatWithTimer", umd_wait);
    // The UMD callback gets the drive state when it changes. The disc here is
    // in and ready from the start, so the callback is told so as soon as it
    // is registered (and again on sceUmdActivate): Felyne Village G waits for
    // that notification before it opens any file on the disc.
    static SceUID umd_callback = 0;
    const auto drive_state = [] {
        constexpr std::uint32_t kPresent = 0x02u;
        constexpr std::uint32_t kReady = 0x10u;
        constexpr std::uint32_t kReadable = 0x20u;
        return io().disc ? (kPresent | kReady | kReadable) : 0x01u;
    };
    hle.add("sceUmdUser", "sceUmdActivate", [drive_state](Runtime &, AllegrexContext &ctx) {
        if (umd_callback != 0) kernel().notify_callback(umd_callback, drive_state());
        kernel().finish(ctx, 0u);
    });
    hle.add("sceUmdUser", "sceUmdRegisterUMDCallBack", [drive_state](Runtime &, AllegrexContext &ctx) {
        umd_callback = static_cast<SceUID>(arg(ctx, 0));
        kernel().notify_callback(umd_callback, drive_state());
        if (trace_io()) std::cerr << "[io] UMD callback " << umd_callback << " registered and notified\n";
        kernel().finish(ctx, 0u);
    });
    hle.add("sceUmdUser", "sceUmdUnRegisterUMDCallBack", [](Runtime &, AllegrexContext &ctx) {
        if (static_cast<SceUID>(arg(ctx, 0)) == umd_callback) umd_callback = 0;
        kernel().finish(ctx, 0u);
    });

    // The data install runs entirely in InitStart; the status then steps
    // through the usual dialog life cycle (hle/utility_dialog.hpp).
    static DialogLifecycle install_dialog;
    hle.add("sceUtility", "sceUtilityGamedataInstallInitStart", [](Runtime &rt, AllegrexContext &ctx) {
        if (install_dialog.active()) {
            kernel().finish(ctx, kErrorUtilityInvalidStatus);
            return;
        }
        const bool ok = install_game_data();
        if (!ok) std::cerr << "[install] the game data install failed; the game keeps reading the disc\n";
        // 0 in the result field: done. A failure is reported the same way for
        // now, since the game can always fall back to the disc.
        rt.memory().store32(arg(ctx, 0) + dialog_common::kResultOffset, 0u);
        install_dialog.start();
        kernel().finish(ctx, 0u);
    });
    hle.add("sceUtility", "sceUtilityGamedataInstallUpdate", [](Runtime &, AllegrexContext &ctx) {
        (void)install_dialog.poll();
        kernel().finish(ctx, 0u);
    });
    hle.add("sceUtility", "sceUtilityGamedataInstallGetStatus",
        [](Runtime &, AllegrexContext &ctx) { kernel().finish(ctx, install_dialog.poll()); });
    const auto install_shutdown = [](Runtime &, AllegrexContext &ctx) {
        (void)install_dialog.shutdown();
        kernel().finish(ctx, 0u);
    };
    hle.add("sceUtility", "sceUtilityGamedataInstallShutdownStart", install_shutdown);
    hle.add("sceUtility", "sceUtilityGamedataInstallAbort", install_shutdown);
}

} // namespace mhp3rd
