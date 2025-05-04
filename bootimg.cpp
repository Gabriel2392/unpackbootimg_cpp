#include "bootimg.h"

namespace {
constexpr uint32_t BOOT_IMAGE_HEADER_V3_PAGESIZE = 4096;
constexpr uint32_t SHA_LENGTH = 32;
constexpr uint32_t BOARDNAME_SIZE = 16;
constexpr uint32_t CMDLINE_SIZE = 512;
constexpr uint32_t EXTRA_CMDLINE_SIZE = 1024;
constexpr uint32_t EXTENDED_CMDLINE_SIZE =
    CMDLINE_SIZE + EXTRA_CMDLINE_SIZE; // v3 and newer = cmdline + extra cmdline
} // namespace

BootImageInfo UnpackBootImage(std::ifstream &input,
                              const std::filesystem::path &output_dir) {
  BootImageInfo info;

  // Read boot magic
  if (!utils::ReadString(input, utils::MAGIC_SIZE, info.boot_magic))
    throw errors::FileReadError("boot magic");

  // Read kernel/ramdisk/second info (9 uint32_t)
  std::array<uint32_t, 9> kernel_ramdisk_second_info;
  for (auto &val : kernel_ramdisk_second_info) {
    if (!utils::ReadU32(input, val))
      throw errors::FileReadError("header information");
  }

  info.header_version = kernel_ramdisk_second_info[8];
  info.page_size = (info.header_version < 3) ? kernel_ramdisk_second_info[7]
                                             : BOOT_IMAGE_HEADER_V3_PAGESIZE;

  // Handle version-specific fields
  uint32_t os_version_patch_level;
  if (info.header_version < 3) {
    info.kernel_size = kernel_ramdisk_second_info[0];
    info.kernel_load_address = kernel_ramdisk_second_info[1];
    info.ramdisk_size = kernel_ramdisk_second_info[2];
    info.ramdisk_load_address = kernel_ramdisk_second_info[3];
    info.second_size = kernel_ramdisk_second_info[4];
    info.second_load_address = kernel_ramdisk_second_info[5];
    info.tags_load_address = kernel_ramdisk_second_info[6];

    if (!utils::ReadU32(input, os_version_patch_level))
      throw errors::FileReadError("os/version patch level");
  } else {
    info.kernel_size = kernel_ramdisk_second_info[0];
    info.ramdisk_size = kernel_ramdisk_second_info[1];
    os_version_patch_level = kernel_ramdisk_second_info[2];
  }

  // Decode OS version/patch level
  auto [os_ver, os_patch] =
      utils::DecodeOsVersionPatchLevel(os_version_patch_level);
  info.os_version = os_ver.value_or("");
  info.os_patch_level = os_patch.value_or("");

  // Handle command line fields
  if (info.header_version < 3) {
    if (!utils::ReadString(input, BOARDNAME_SIZE, info.product_name))
      throw errors::FileReadError("board name");

    if (!utils::ReadString(input, CMDLINE_SIZE, info.cmdline))
      throw errors::FileReadError("boot cmdline");

    input.seekg(SHA_LENGTH, std::ios::cur);
    if (!input)
      throw errors::FileReadError("SHA-1 checksum");

    if (!utils::ReadString(input, EXTRA_CMDLINE_SIZE, info.extra_cmdline))
      throw errors::FileReadError("boot extra cmdline");
  } else {
    if (!utils::ReadString(input, EXTENDED_CMDLINE_SIZE, info.cmdline))
      throw errors::FileReadError("boot cmdline");
  }

  // Handle version-specific extensions
  if (info.header_version == 1 || info.header_version == 2) {
    if (!utils::ReadU32(input, info.recovery_dtbo_size))
      throw errors::FileReadError("recovery_dtbo_size");
    if (!utils::ReadU64(input, info.recovery_dtbo_offset))
      throw errors::FileReadError("recovery_dtbo_offset");
    if (!utils::ReadU32(input, info.boot_header_size))
      throw errors::FileReadError("boot_header_size");
  }

  if (info.header_version == 2) {
    if (!utils::ReadU32(input, info.dtb_size))
      throw errors::FileReadError("dtb_size");
    if (!utils::ReadU64(input, info.dtb_load_address))
      throw errors::FileReadError("dtb_load_address");
  }

  if (info.header_version >= 4) {
    if (!utils::ReadU32(input, info.boot_signature_size))
      throw errors::FileReadError("boot_signature_size");
  }

  // Calculate image offsets
  std::vector<utils::ImageEntry> image_entries;
  const uint32_t page_size = info.page_size;
  const uint32_t num_header_pages = 1;

  // Kernel
  uint32_t num_kernel_pages =
      utils::GetNumberOfPages(info.kernel_size, page_size);
  if (info.kernel_size > 0) { // Patch1: Only unpack kernel if it exists
    image_entries.emplace_back(page_size * num_header_pages, info.kernel_size,
                               "kernel");
  }

  // Ramdisk
  uint32_t num_ramdisk_pages =
      utils::GetNumberOfPages(info.ramdisk_size, page_size);
  if (info.ramdisk_size > 0) { // Patch2: Only unpack ramdisk if it exists
    image_entries.emplace_back(page_size *
                                   (num_header_pages + num_kernel_pages),
                               info.ramdisk_size, "ramdisk");
  }

  // Second
  if (info.second_size > 0) {
    image_entries.emplace_back(
        page_size * (num_header_pages + num_kernel_pages + num_ramdisk_pages),
        info.second_size, "second");
  }

  // Recovery DTBO
  if (info.recovery_dtbo_size > 0) {
    image_entries.emplace_back(info.recovery_dtbo_offset,
                               info.recovery_dtbo_size, "recovery_dtbo");
  }

  // DTB
  if (info.dtb_size > 0) {
    image_entries.emplace_back(
        page_size *
            (num_header_pages + num_kernel_pages + num_ramdisk_pages +
             utils::GetNumberOfPages(info.second_size, page_size) +
             utils::GetNumberOfPages(info.recovery_dtbo_size, page_size)),
        info.dtb_size, "dtb");
  }

  // Boot signature
  if (info.boot_signature_size > 0) {
    image_entries.emplace_back(
        page_size * (num_header_pages + num_kernel_pages + num_ramdisk_pages),
        info.boot_signature_size, "boot_signature");
  }

  // Create output directory
  if (!utils::CreateDirectory(output_dir))
    throw std::runtime_error("Could not create output directory.");

  // Extract images
  for (const auto &entry : image_entries) {
    const auto output_path = output_dir / entry.name;
    if (!utils::ExtractImage(input, entry.offset, entry.size, output_path)) {
      throw std::runtime_error("Could not extract image: " + entry.name);
    }
  }

  info.image_dir = output_dir;
  return info;
}

std::string FormatPrettyText(const BootImageInfo &info) {
  std::ostringstream oss;
  oss << "boot magic: " << info.boot_magic << "\n";

  if (info.header_version < 3) {
    oss << "kernel_size: " << info.kernel_size << "\n"
        << "kernel load address: 0x" << std::hex << info.kernel_load_address
        << "\n"
        << "ramdisk size: " << std::dec << info.ramdisk_size << "\n"
        << "ramdisk load address: 0x" << std::hex << info.ramdisk_load_address
        << "\n"
        << "second bootloader size: " << std::dec << info.second_size << "\n"
        << "second bootloader load address: 0x" << std::hex
        << info.second_load_address << "\n"
        << "kernel tags load address: 0x" << info.tags_load_address << "\n";
  }

  oss << "page size: " << std::dec << info.page_size << "\n"
      << "os version: " << info.os_version << "\n"
      << "os patch level: " << info.os_patch_level << "\n"
      << "boot image header version: " << info.header_version << "\n";

  if (info.header_version < 3) {
    oss << "product name: " << info.product_name << "\n";
  }

  oss << "command line args: " << info.cmdline << "\n";

  if (info.header_version < 3) {
    oss << "additional command line args: " << info.extra_cmdline << "\n";
  }

  if (info.header_version == 1 || info.header_version == 2) {
    oss << "recovery dtbo size: " << info.recovery_dtbo_size << "\n"
        << "recovery dtbo offset: 0x" << std::hex << info.recovery_dtbo_offset
        << "\n"
        << "boot header size: " << std::dec << info.boot_header_size << "\n";
  }

  if (info.header_version == 2) {
    oss << "dtb size: " << info.dtb_size << "\n"
        << "dtb address: 0x" << std::hex << info.dtb_load_address << "\n";
  }

  if (info.header_version >= 4) {
    oss << "boot.img signature size: " << std::dec << info.boot_signature_size
        << "\n";
  }

  return oss.str();
}

std::vector<std::string> FormatMkbootimgArguments(const BootImageInfo &info) {
  std::vector<std::string> args;

  auto add_arg = [&](const std::string &option, const std::string &value) {
    args.emplace_back(option);
    args.emplace_back(value);
  };

  add_arg("--header_version", std::to_string(info.header_version));

  if (!info.os_version.empty()) {
    add_arg("--os_version", info.os_version);
  }

  if (!info.os_patch_level.empty()) {
    add_arg("--os_patch_level", info.os_patch_level);
  }

  if (info.kernel_size > 0) {
    add_arg("--kernel", (info.image_dir / "kernel").string());
  }

  if (info.ramdisk_size > 0) {
    add_arg("--ramdisk", (info.image_dir / "ramdisk").string());
  }

  if (info.header_version == 2) {
    add_arg("--dtb", (info.image_dir / "dtb").string());
    add_arg("--dtb_offset", std::to_string(info.dtb_load_address));
  }

  if (info.header_version <= 2) {
    if (info.second_size > 0) {
      add_arg("--second", (info.image_dir / "second").string());
    }

    if (info.recovery_dtbo_size > 0) {
      add_arg("--recovery_dtbo", (info.image_dir / "recovery_dtbo").string());
    }

    add_arg("--pagesize", std::to_string(info.page_size));
    add_arg("--base", "0x0");

    add_arg("--kernel_offset", std::format("0x{:x}", info.kernel_load_address));
    add_arg("--ramdisk_offset",
            std::format("0x{:x}", info.ramdisk_load_address));

    if (info.header_version == 2) {
      add_arg("--dtb_offset", std::format("0x{:x}", info.dtb_load_address));
    }

    add_arg("--board", info.product_name);
    add_arg("--cmdline", info.cmdline + info.extra_cmdline);
  } else {
    add_arg("--cmdline", info.cmdline);
  }

  return args;
}
