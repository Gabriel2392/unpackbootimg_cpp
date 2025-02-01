#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace utils {

bool CreateDirectory(const std::filesystem::path& dir_path);
bool ExtractImage(std::ifstream& input, uint64_t offset, uint64_t size, 
                  const std::filesystem::path& output_path);
inline uint32_t GetNumberOfPages(uint32_t image_size, uint32_t page_size) {
    return (image_size + page_size - 1) / page_size;
}
std::string CStr(std::string_view s);
std::optional<std::string> FormatOsVersion(uint32_t os_version);
std::optional<std::string> FormatOsPatchLevel(uint32_t os_patch_level);

struct OsVersionPatchLevel {
    std::optional<std::string> os_version;
    std::optional<std::string> os_patch_level;
};
OsVersionPatchLevel DecodeOsVersionPatchLevel(uint32_t os_version_patch_level);

bool ReadU32(std::istream& stream, uint32_t& value);
bool ReadU64(std::istream& stream, uint64_t& value);
std::optional<std::string> ReadString(std::istream& stream, size_t length);

} // namespace utils
