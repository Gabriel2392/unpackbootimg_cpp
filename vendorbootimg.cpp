#include "vendorbootimg.h"

namespace {
constexpr uint32_t VENDOR_RAMDISK_NAME_SIZE = 32;
constexpr uint32_t CMDLINE_SIZE = 2048;
constexpr uint32_t BOARDNAME_SIZE = 16;
} // namespace

VendorBootImageInfo
UnpackVendorBootImage(std::ifstream &input,
                      const std::filesystem::path &output_dir) {
  VendorBootImageInfo info;

  // Read header fields
  if (!(utils::ReadString(input, utils::MAGIC_SIZE, info.boot_magic) &&
        utils::ReadU32(input, info.header_version) &&
        utils::ReadU32(input, info.page_size) &&
        utils::ReadU32(input, info.kernel_load_address) &&
        utils::ReadU32(input, info.ramdisk_load_address) &&
        utils::ReadU32(input, info.vendor_ramdisk_size) &&
        utils::ReadString(input, CMDLINE_SIZE, info.cmdline) &&
        utils::ReadU32(input, info.tags_load_address) &&
        utils::ReadString(input, BOARDNAME_SIZE, info.product_name) &&
        utils::ReadU32(input, info.header_size) &&
        utils::ReadU32(input, info.dtb_size) &&
        utils::ReadU64(input, info.dtb_load_address))) {
    throw errors::FileReadError("header information");
  }

  info.cmdline = utils::CStr(info.cmdline);
  info.product_name = utils::CStr(info.product_name);

  // Handle version >3 fields
  if (info.header_version > 3) {
    if (!(utils::ReadU32(input, info.vendor_ramdisk_table_size) &&
          utils::ReadU32(input, info.vendor_ramdisk_table_entry_num) &&
          utils::ReadU32(input, info.vendor_ramdisk_table_entry_size) &&
          utils::ReadU32(input, info.vendor_bootconfig_size))) {
      throw errors::FileReadError("ramdisk table");
    }
  }

  // Calculate offsets
  const uint32_t page_size = info.page_size;
  const uint32_t num_header_pages =
      utils::GetNumberOfPages(info.header_size, page_size);
  const uint32_t ramdisk_offset_base = page_size * num_header_pages;

  std::vector<utils::ImageEntry> image_entries;
  std::vector<std::pair<std::string, std::string>> vendor_ramdisk_symlinks;

  // Handle vendor ramdisk table
  if (info.header_version > 3) {
    const uint32_t table_pages =
        utils::GetNumberOfPages(info.vendor_ramdisk_table_size, page_size);
    const uint64_t table_offset =
        page_size *
        (num_header_pages +
         utils::GetNumberOfPages(info.vendor_ramdisk_size, page_size) +
         utils::GetNumberOfPages(info.dtb_size, page_size));

    for (uint32_t i = 0; i < info.vendor_ramdisk_table_entry_num; ++i) {
      const uint64_t entry_offset =
          table_offset + (info.vendor_ramdisk_table_entry_size * i);
      input.seekg(entry_offset);

      VendorRamdiskTableEntry entry;
      if (!(utils::ReadU32(input, entry.size) &&
            utils::ReadU32(input, entry.offset) &&
            utils::ReadU32(input, entry.type) &&
            utils::ReadString(input, VENDOR_RAMDISK_NAME_SIZE, entry.name) &&
            utils::ReadU32Array(input, entry.board_id))) {
        throw errors::FileReadError("ramdisk: " + entry.name);
      }

      entry.output_name = std::format("vendor_ramdisk{:02}", i);
      entry.name = utils::CStr(entry.name);

      image_entries.emplace_back(ramdisk_offset_base + entry.offset, entry.size,
                                 entry.output_name);

      vendor_ramdisk_symlinks.emplace_back(entry.output_name, entry.name);
      info.vendor_ramdisk_table.push_back(std::move(entry));
    }

    // Handle bootconfig
    const uint64_t bootconfig_offset =
        page_size *
        (num_header_pages +
         utils::GetNumberOfPages(info.vendor_ramdisk_size, page_size) +
         utils::GetNumberOfPages(info.dtb_size, page_size) + table_pages);
    image_entries.emplace_back(bootconfig_offset, info.vendor_bootconfig_size,
                               "bootconfig");
  } else {
    image_entries.emplace_back(ramdisk_offset_base, info.vendor_ramdisk_size,
                               "vendor_ramdisk");
  }

  // Handle DTB
  if (info.dtb_size > 0) {
    const uint64_t dtb_offset =
        page_size *
        (num_header_pages +
         utils::GetNumberOfPages(info.vendor_ramdisk_size, page_size));
    image_entries.emplace_back(dtb_offset, info.dtb_size, "dtb");
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

  // Create symlinks for vendor ramdisks
  if (info.header_version > 3 && !vendor_ramdisk_symlinks.empty()) {
    const auto symlink_dir = output_dir / "vendor-ramdisk-by-name";
    if (!utils::CreateDirectory(symlink_dir))
      throw std::runtime_error("Could not create symlink directory.");

    for (const auto &[src, dst] : vendor_ramdisk_symlinks) {
      const auto src_path =
          std::filesystem::relative(output_dir / src, symlink_dir);
      const auto dst_path = symlink_dir / std::format("ramdisk_{}", dst);

      std::error_code ec;
      std::filesystem::remove(dst_path, ec);
      std::filesystem::create_symlink(src_path, dst_path, ec);
    }
  }

  info.image_dir = output_dir;
  return info;
}

std::string FormatPrettyText(const VendorBootImageInfo &info) {
  std::ostringstream oss;
  oss << "boot magic: " << info.boot_magic << "\n"
      << "vendor boot image header version: " << info.header_version << "\n"
      << "page size: " << info.page_size << "\n"
      << "kernel load address: 0x" << std::hex << info.kernel_load_address
      << "\n"
      << "ramdisk load address: 0x" << info.ramdisk_load_address << "\n";

  if (info.header_version > 3) {
    oss << "vendor ramdisk total size: " << std::dec << info.vendor_ramdisk_size
        << "\n";
  } else {
    oss << "vendor ramdisk size: " << std::dec << info.vendor_ramdisk_size
        << "\n";
  }

  oss << "vendor command line args: " << info.cmdline << "\n"
      << "kernel tags load address: 0x" << std::hex << info.tags_load_address
      << "\n"
      << "product name: " << info.product_name << "\n"
      << "vendor boot image header size: " << std::dec << info.header_size
      << "\n"
      << "dtb size: " << info.dtb_size << "\n"
      << "dtb address: 0x" << std::hex << info.dtb_load_address << "\n";

  if (info.header_version > 3) {
    oss << "vendor ramdisk table size: " << std::dec
        << info.vendor_ramdisk_table_size << "\n"
        << "vendor ramdisk table:\n[\n";

    for (const auto &entry : info.vendor_ramdisk_table) {
      oss << std::dec << "    " << entry.output_name << ": {\n"
          << "        size: " << entry.size << "\n"
          << "        offset: " << entry.offset << "\n"
          << "        type: " << std::hex << utils::getRamdiskType(entry.type)
          << "\n"
          << "        name: " << entry.name << "\n"
          << "        board_id: [\n";

      for (size_t i = 0; i < entry.board_id.size(); i += 4) {
        oss << "            ";
        for (size_t j = 0; j < 4 && (i + j) < entry.board_id.size(); ++j) {
          oss << std::hex << "0x" << entry.board_id[i + j] << ", ";
        }
        oss << "\n";
      }
      oss << "        ]\n    }\n";
    }
    oss << "]\n"
        << "vendor bootconfig size: " << std::dec << info.vendor_bootconfig_size
        << "\n";
  }

  return oss.str();
}

std::vector<std::string>
FormatMkbootimgArguments(const VendorBootImageInfo &info) {
  std::vector<std::string> args;

  auto add_arg = [&](const std::string &option, const std::string &value) {
    args.emplace_back(option);
    args.emplace_back(value);
  };

  add_arg("--header_version", std::to_string(info.header_version));

  add_arg("--pagesize", std::format("0x{:x}", info.page_size));
  add_arg("--base", "0x0");
  add_arg("--kernel_offset", std::format("0x{:x}", info.kernel_load_address));
  add_arg("--ramdisk_offset", std::format("0x{:x}", info.ramdisk_load_address));
  add_arg("--tags_offset", std::format("0x{:x}", info.tags_load_address));
  add_arg("--dtb_offset", std::format("0x{:x}", info.dtb_load_address));

  if (!info.cmdline.empty()) add_arg("--vendor_cmdline", info.cmdline);
  if (!info.product_name.empty()) add_arg("--board", info.product_name);

  if (info.dtb_size > 0) {
    add_arg("--dtb", (info.image_dir / "dtb").string());
  }

  if (info.header_version > 3) {
    add_arg("--vendor_bootconfig", (info.image_dir / "bootconfig").string());

    for (const auto &entry : info.vendor_ramdisk_table) {
      if (entry.name.empty()) {
        add_arg("--vendor_ramdisk",
                (info.image_dir / entry.output_name).string());
        continue;
      }
      add_arg("--ramdisk_type", utils::getRamdiskType(entry.type));
      add_arg("--ramdisk_name", entry.name);

      // Board ID is not supported atm, but this is for priting
      /* for (size_t i = 0; i < entry.board_id.size(); ++i) {
          if (entry.board_id[i] != 0) {
              add_arg(std::format("--board_id{}", i), std::format("{:#x}",
      entry.board_id[i]));
          }
      } */
      add_arg("--vendor_ramdisk_fragment",
              (info.image_dir / entry.output_name).string());
    }
  } else {
    add_arg("--vendor_ramdisk", (info.image_dir / "vendor_ramdisk").string());
  }

  return args;
}
