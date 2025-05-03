#pragma once

#include "utils.hpp"

struct VendorRamdiskTableEntry {
  std::string output_name;
  uint32_t size;
  uint32_t offset;
  uint32_t type;
  std::string name;
  std::array<uint32_t, 4> board_id; // 16 bytes = 4 * uint32_t
};

struct VendorBootImageInfo {
  std::string boot_magic;
  uint32_t header_version = 0;
  uint32_t page_size = 4096;
  uint32_t kernel_load_address = 0;
  uint32_t ramdisk_load_address = 0;
  uint32_t vendor_ramdisk_size = 0;
  std::string cmdline;
  uint32_t tags_load_address = 0;
  std::string product_name;
  uint32_t header_size = 0;
  uint32_t dtb_size = 0;
  uint64_t dtb_load_address = 0;

  // Version >3 fields
  uint32_t vendor_ramdisk_table_size = 0;
  uint32_t vendor_ramdisk_table_entry_num = 0;
  uint32_t vendor_ramdisk_table_entry_size = 0;
  uint32_t vendor_bootconfig_size = 0;
  std::vector<VendorRamdiskTableEntry> vendor_ramdisk_table;

  std::filesystem::path image_dir;
};

VendorBootImageInfo
UnpackVendorBootImage(std::ifstream &input,
                      const std::filesystem::path &output_dir);

std::string FormatPrettyText(const VendorBootImageInfo &info);
std::vector<std::string>
FormatMkbootimgArguments(const VendorBootImageInfo &info);
