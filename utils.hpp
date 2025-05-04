#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <istream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <unordered_map>

namespace utils {
constexpr uint32_t MAGIC_SIZE = 8;

inline std::string getRamdiskType(uint32_t type) {
    static const std::unordered_map<uint32_t, std::string> ramdiskMap = {
        {0, "none"},
        {1, "platform"},
        {2, "recovery"},
        {3, "dlkm"},
    };

    auto it = ramdiskMap.find(type);
    return (it != ramdiskMap.end()) ? it->second : "none";
}

inline bool CreateDirectory(const std::filesystem::path &dir_path) {
  std::error_code ec;
  std::filesystem::create_directories(dir_path, ec);
  return !ec;
}

inline bool ExtractImage(std::ifstream &input, uint64_t offset, uint64_t size,
                         const std::filesystem::path &output_path) {
  if (size == 0) {
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    return output.good();
  }

  if (!input.seekg(offset)) {
    return false;
  }

  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  constexpr std::streamsize kChunkSize = 65536;
  std::vector<char> buffer(kChunkSize);
  uint64_t remaining = size;

  while (remaining > 0) {
    const std::streamsize bytes_to_read = static_cast<std::streamsize>(
        std::min(remaining, static_cast<uint64_t>(kChunkSize)));

    if (!input.read(buffer.data(), bytes_to_read)) {
      return false;
    }

    if (!output.write(buffer.data(), bytes_to_read)) {
      return false;
    }

    remaining -= static_cast<uint64_t>(bytes_to_read);
  }

  return output.good();
}

inline uint32_t GetNumberOfPages(uint32_t image_size, uint32_t page_size) {
  if (page_size == 0) {
    return 0;
  }
  return (image_size + page_size - 1) / page_size;
}

inline std::string CStr(std::string_view s) {
  if (auto pos = s.find('\0'); pos != std::string_view::npos) {
    return std::string(s.substr(0, pos));
  }
  return std::string(s);
}

inline std::optional<std::string> FormatOsVersion(uint32_t os_version) {
  if (os_version == 0) {
    return std::nullopt;
  }
  // A = (os_version >> 14) & 0x7F; // bits 14-20
  // B = (os_version >> 7) & 0x7F; // bits 7-13
  // C = os_version & 0x7F; // bits 0-6
  // Major version (A) is bits 25-31 (7 bits)
  // Minor version (B) is bits 18-24 (7 bits)
  // Patch version (C) is bits 11-17 (7 bits)
  uint32_t a = os_version >> 14;
  uint32_t b = (os_version >> 7) & 0x7F;
  uint32_t c = os_version & 0x7F;

  char buffer[32];
  unsigned long written =
      std::snprintf(buffer, sizeof(buffer), "%u.%u.%u", a, b, c);

  if (written > 0 && written < sizeof(buffer)) {
    return std::string(buffer);
  } else {
    return std::nullopt;
  }
}

inline std::optional<std::string> FormatOsPatchLevel(uint32_t os_patch_level) {
  if (os_patch_level == 0) {
    return std::nullopt;
  }
  // Year = 2000 + (os_patch_level >> 4) // bits 4-10
  // Month = os_patch_level & 0x0F // bits 0-3
  uint32_t y = (os_patch_level >> 4) + 2000;
  uint32_t m = os_patch_level & 0x0F;

  if (m < 1 || m > 12) {
    return std::nullopt;
  }

  char buffer[8];
  int written = std::snprintf(buffer, sizeof(buffer), "%04u-%02u", y, m);

  if (written == 7) {
    return std::string(buffer);
  } else {
    return std::nullopt;
  }
}

struct OsVersionPatchLevel {
  std::optional<std::string> os_version;
  std::optional<std::string> os_patch_level;
};

inline OsVersionPatchLevel
DecodeOsVersionPatchLevel(uint32_t os_version_patch_level) {
  // Standard split: version = high 21 bits, patch = low 11 bits
  uint32_t version_val = os_version_patch_level >> 11;
  uint32_t patch_val = os_version_patch_level & 0x7FF; // 11 bits mask
  return {FormatOsVersion(version_val), FormatOsPatchLevel(patch_val)};
}

inline bool ReadU32(std::istream &stream, uint32_t &value) {
  uint8_t bytes[4];
  if (!stream.read(reinterpret_cast<char *>(bytes), sizeof(bytes))) {
    return false;
  }
  value = static_cast<uint32_t>(bytes[0]) |
          (static_cast<uint32_t>(bytes[1]) << 8) |
          (static_cast<uint32_t>(bytes[2]) << 16) |
          (static_cast<uint32_t>(bytes[3]) << 24);
  return true;
}

inline bool ReadU64(std::istream &stream, uint64_t &value) {
  uint8_t bytes[8];
  if (!stream.read(reinterpret_cast<char *>(bytes), sizeof(bytes))) {
    return false;
  }
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

inline bool ReadString(std::istream &stream, size_t length, std::string &out) {
  if (length == 0) {
    out.clear();
    return true; // Read zero bytes successfully
  }
  if (length > std::numeric_limits<std::streamsize>::max()) {
    return false; // Avoid potential overflow issues with stream read size
  }

  out.resize(length); // Resize string to hold the data
  if (!stream.read(out.data(), static_cast<std::streamsize>(length))) {
    // Check if EOF was reached but some data was read
    if (!stream.eof() || stream.gcount() == 0) {
      out.clear();  // Clear output string on error
      return false; // Return error if not EOF or nothing was read
    }
    // If EOF reached, resize to the actual number of bytes read
    out.resize(static_cast<size_t>(stream.gcount()));
  }

  // Find the first null terminator and resize the string accordingly
  if (auto pos = out.find('\0'); pos != std::string::npos) {
    out.resize(pos);
  }
  return true;
}

template <size_t N>
inline bool ReadU32Array(std::istream &stream, std::array<uint32_t, N> &arr) {
  for (auto &val : arr) {
    if (!ReadU32(stream, val)) {
      return false;
    }
  }
  return true;
}

struct ImageEntry {
  uint64_t offset;
  uint32_t size;
  std::string name;

  ImageEntry(uint64_t o, uint32_t s, std::string n)
      : offset(o), size(s), name(std::move(n)) {}
};

} // namespace utils

namespace errors {

class FileReadError : public std::runtime_error {
private:
  std::string context_info_;

  static std::string build_message(const std::string &context) {
    return "Error while reading " + context +
           " (premature end or corrupt file)";
  }

public:
  explicit FileReadError(const std::string &context)
      : std::runtime_error(build_message(context)), context_info_(context) {}

  explicit FileReadError(const char *context)
      : std::runtime_error(build_message(context)), context_info_(context) {}

  const std::string &get_context() const noexcept { return context_info_; }
};

} // namespace errors
