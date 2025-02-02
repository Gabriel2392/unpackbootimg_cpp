#include "utils.h"
#include <cstdio>
#include <system_error>

namespace utils {

bool CreateDirectory(const std::filesystem::path &dir_path) {
  std::error_code ec;
  std::filesystem::create_directories(dir_path, ec);
  return !ec;
}

bool ExtractImage(std::ifstream &input, uint64_t offset, uint64_t size,
                  const std::filesystem::path &output_path) {
  input.seekg(offset);
  if (!input)
    return false;

  std::vector<char> buffer(size);
  if (!input.read(buffer.data(), size))
    return false;

  std::ofstream output(output_path, std::ios::binary);
  if (!output)
    return false;

  return output.write(buffer.data(), size).good();
}

std::string CStr(std::string_view s) {
  if (auto pos = s.find('\0'); pos != s.npos)
    return std::string(s.substr(0, pos));
  return std::string(s);
}

std::optional<std::string> FormatOsVersion(uint32_t os_version) {
  if (os_version == 0)
    return std::nullopt;
  uint32_t a = os_version >> 14;
  uint32_t b = (os_version >> 7) & 0x7F;
  uint32_t c = os_version & 0x7F;
  return std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c);
}

std::optional<std::string> FormatOsPatchLevel(uint32_t os_patch_level) {
  if (os_patch_level == 0)
    return std::nullopt;
  uint32_t y = (os_patch_level >> 4) + 2000;
  uint32_t m = os_patch_level & 0x0F;
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d", y, m);
  return std::string(buffer);
}

OsVersionPatchLevel DecodeOsVersionPatchLevel(uint32_t os_version_patch_level) {
  return {FormatOsVersion(os_version_patch_level >> 11),
          FormatOsPatchLevel(os_version_patch_level & 0x7FF)};
}

bool ReadU32(std::istream &stream, uint32_t &value) {
  uint8_t bytes[4];
  if (!stream.read(reinterpret_cast<char *>(bytes), sizeof(bytes)))
    return false;
  value = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
  return true;
}

bool ReadU64(std::istream &stream, uint64_t &value) {
  uint8_t bytes[8];
  if (!stream.read(reinterpret_cast<char *>(bytes), sizeof(bytes)))
    return false;
  value = static_cast<uint64_t>(bytes[0]) |
          (static_cast<uint64_t>(bytes[1]) << 8) |
          (static_cast<uint64_t>(bytes[2]) << 16) |
          (static_cast<uint64_t>(bytes[3]) << 24) |
          (static_cast<uint64_t>(bytes[4]) << 32) |
          (static_cast<uint64_t>(bytes[5]) << 40) |
          (static_cast<uint64_t>(bytes[6]) << 48) |
          (static_cast<uint64_t>(bytes[7]) << 56);
  return true;
}

std::optional<std::string> ReadString(std::istream &stream, size_t length) {
  std::string s(length, '\0');
  if (!stream.read(s.data(), length))
    return std::nullopt;
  return CStr(s);
}

bool ReadString(std::istream &stream, size_t length, std::string &out) {
  std::string s(length, '\0');
  if (!stream.read(s.data(), length)) {
    return false;
  }
  out = CStr(s);
  return true;
}

} // namespace utils
