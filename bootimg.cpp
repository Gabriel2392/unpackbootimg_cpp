#include "bootimg.h"
#include "utils.h"
#include <array>
#include <sstream>
#include <vector>

namespace {
    constexpr uint32_t BOOT_IMAGE_HEADER_V3_PAGESIZE = 4096;
    constexpr uint32_t BOOT_MAGIC_SIZE = 8;
    constexpr uint32_t SHA_LENGTH = 32;

    struct ImageEntry {
        uint64_t offset;
        uint32_t size;
        std::string name;
    };
}

std::optional<BootImageInfo> UnpackBootImage(std::ifstream& input,
                                            const std::filesystem::path& output_dir) {
    BootImageInfo info;
    
    // Read boot magic
    if (auto magic = utils::ReadString(input, BOOT_MAGIC_SIZE)) {
        info.boot_magic = *magic;
    } else {
        return std::nullopt;
    }

    // Read kernel/ramdisk/second info (9 uint32_t)
    std::array<uint32_t, 9> kernel_ramdisk_second_info;
    for (auto& val : kernel_ramdisk_second_info) {
        if (!utils::ReadU32(input, val)) return std::nullopt;
    }
    
    info.header_version = kernel_ramdisk_second_info[8];
    info.page_size = (info.header_version < 3) 
        ? kernel_ramdisk_second_info[7] 
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
        
        if (!utils::ReadU32(input, os_version_patch_level)) return std::nullopt;
    } else {
        info.kernel_size = kernel_ramdisk_second_info[0];
        info.ramdisk_size = kernel_ramdisk_second_info[1];
        os_version_patch_level = kernel_ramdisk_second_info[2];
    }

    // Decode OS version/patch level
    auto [os_ver, os_patch] = utils::DecodeOsVersionPatchLevel(os_version_patch_level);
    info.os_version = os_ver.value_or("");
    info.os_patch_level = os_patch.value_or("");

    // Handle command line fields
    if (info.header_version < 3) {
        if (auto product = utils::ReadString(input, 16)) 
            info.product_name = utils::CStr(*product);
        if (auto cmdline = utils::ReadString(input, 512))
            info.cmdline = utils::CStr(*cmdline);
        
        input.seekg(SHA_LENGTH, std::ios::cur);
        if (!input) return std::nullopt;
        
        if (auto extra = utils::ReadString(input, 1024))
            info.extra_cmdline = utils::CStr(*extra);
    } else {
        if (auto cmdline = utils::ReadString(input, 1536))
            info.cmdline = utils::CStr(*cmdline);
    }

    // Handle version-specific extensions
    if (info.header_version == 1 || info.header_version == 2) {
        if (!utils::ReadU32(input, info.recovery_dtbo_size)) return std::nullopt;
        if (!utils::ReadU64(input, info.recovery_dtbo_offset)) return std::nullopt;
        if (!utils::ReadU32(input, info.boot_header_size)) return std::nullopt;
    }

    if (info.header_version == 2) {
        if (!utils::ReadU32(input, info.dtb_size)) return std::nullopt;
        if (!utils::ReadU64(input, info.dtb_load_address)) return std::nullopt;
    }

    if (info.header_version >= 4) {
        if (!utils::ReadU32(input, info.boot_signature_size)) return std::nullopt;
    }

    // Calculate image offsets
    std::vector<ImageEntry> image_entries;
    const uint32_t page_size = info.page_size;
    const uint32_t num_header_pages = 1;

    // Kernel
    uint32_t num_kernel_pages = utils::GetNumberOfPages(info.kernel_size, page_size);
    if (info.kernel_size > 0) { // Patch1: Only unpack kernel if it exists
        image_entries.emplace_back(
            page_size * num_header_pages,
            info.kernel_size,
            "kernel"
        );
    }

    // Ramdisk
    uint32_t num_ramdisk_pages = utils::GetNumberOfPages(info.ramdisk_size, page_size);
    image_entries.emplace_back(
        page_size * (num_header_pages + num_kernel_pages),
        info.ramdisk_size,
        "ramdisk"
    );

    // Second
    if (info.second_size > 0) {
        image_entries.emplace_back(
            page_size * (num_header_pages + num_kernel_pages + num_ramdisk_pages),
            info.second_size,
            "second"
        );
    }

    // Recovery DTBO
    if (info.recovery_dtbo_size > 0) {
        image_entries.emplace_back(
            info.recovery_dtbo_offset,
            info.recovery_dtbo_size,
            "recovery_dtbo"
        );
    }

    // DTB
    if (info.dtb_size > 0) {
        image_entries.emplace_back(
            page_size * (num_header_pages + num_kernel_pages + 
                        num_ramdisk_pages + utils::GetNumberOfPages(info.second_size, page_size) +
                        utils::GetNumberOfPages(info.recovery_dtbo_size, page_size)),
            info.dtb_size,
            "dtb"
        );
    }

    // Boot signature
    if (info.boot_signature_size > 0) {
        image_entries.emplace_back(
            page_size * (num_header_pages + num_kernel_pages + num_ramdisk_pages),
            info.boot_signature_size,
            "boot_signature"
        );
    }

    // Create output directory
    if (!utils::CreateDirectory(output_dir)) return std::nullopt;

    // Extract images
    for (const auto& entry : image_entries) {
        const auto output_path = output_dir / entry.name;
        if (!utils::ExtractImage(input, entry.offset, entry.size, output_path)) {
            return std::nullopt;
        }
    }

    info.image_dir = output_dir;
    return info;
}

std::string FormatPrettyText(const BootImageInfo& info) {
    std::ostringstream oss;
    oss << "boot magic: " << info.boot_magic << "\n";

    if (info.header_version < 3) {
        oss << "kernel_size: " << info.kernel_size << "\n"
            << "kernel load address: 0x" << std::hex << info.kernel_load_address << "\n"
            << "ramdisk size: " << std::dec << info.ramdisk_size << "\n"
            << "ramdisk load address: 0x" << std::hex << info.ramdisk_load_address << "\n"
            << "second bootloader size: " << std::dec << info.second_size << "\n"
            << "second bootloader load address: 0x" << std::hex << info.second_load_address << "\n"
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
            << "recovery dtbo offset: 0x" << std::hex << info.recovery_dtbo_offset << "\n"
            << "boot header size: " << std::dec << info.boot_header_size << "\n";
    }

    if (info.header_version == 2) {
        oss << "dtb size: " << info.dtb_size << "\n"
            << "dtb address: 0x" << std::hex << info.dtb_load_address << "\n";
    }

    if (info.header_version >= 4) {
        oss << "boot.img signature size: " << std::dec << info.boot_signature_size << "\n";
    }

    return oss.str();
}

std::vector<std::string> FormatMkbootimgArguments(const BootImageInfo& info) {
    std::vector<std::string> args;
    args.emplace_back("--header_version");
    args.emplace_back(std::to_string(info.header_version));

    if (!info.os_version.empty()) {
        args.emplace_back("--os_version");
        args.emplace_back(info.os_version);
    }

    if (!info.os_patch_level.empty()) {
        args.emplace_back("--os_patch_level");
        args.emplace_back(info.os_patch_level);
    }

    const auto kernel_path = (info.image_dir / "kernel").string();
    args.emplace_back("--kernel");
    args.emplace_back(kernel_path);

    const auto ramdisk_path = (info.image_dir / "ramdisk").string();
    args.emplace_back("--ramdisk");
    args.emplace_back(ramdisk_path);

    if (info.header_version <= 2) {
        if (info.second_size > 0) {
            args.emplace_back("--second");
            args.emplace_back((info.image_dir / "second").string());
        }

        if (info.recovery_dtbo_size > 0) {
            args.emplace_back("--recovery_dtbo");
            args.emplace_back((info.image_dir / "recovery_dtbo").string());
        }

        args.emplace_back("--pagesize");
        args.emplace_back(std::to_string(info.page_size));

        args.emplace_back("--base");
        args.emplace_back("0x0");

        args.emplace_back("--kernel_offset");
        args.emplace_back(std::format("0x{:x}", info.kernel_load_address));

        args.emplace_back("--ramdisk_offset");
        args.emplace_back(std::format("0x{:x}", info.ramdisk_load_address));

        if (info.header_version == 2) {
            args.emplace_back("--dtb_offset");
            args.emplace_back(std::format("0x{:x}", info.dtb_load_address));
        }

        args.emplace_back("--board");
        args.emplace_back(info.product_name);

        args.emplace_back("--cmdline");
        args.emplace_back(info.cmdline + info.extra_cmdline);
    } else {
        args.emplace_back("--cmdline");
        args.emplace_back(info.cmdline);
    }

    return args;
}
