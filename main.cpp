#include "bootimg.h"
#include "utils.h"
#include "vendorbootimg.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ProgramArgs {
  fs::path boot_img;
  fs::path output_dir = "out";
  std::string format = "info";
  bool null_separator = false;
};

std::optional<ProgramArgs> ParseArguments(int argc, char *argv[]) {
  ProgramArgs args;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--boot_img" && ++i < argc) {
      args.boot_img = argv[i];
    } else if (arg == "--out" && ++i < argc) {
      args.output_dir = argv[i];
    } else if (arg == "--format" && ++i < argc) {
      args.format = argv[i];
      if (args.format != "info" && args.format != "mkbootimg") {
        std::cerr << "Invalid format: " << args.format << "\n";
        return std::nullopt;
      }
    } else if (arg == "-0" || arg == "--null") {
      args.null_separator = true;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return std::nullopt;
    }
  }

  if (args.boot_img.empty()) {
    std::cerr << "Missing required --boot_img argument\n";
    return std::nullopt;
  }

  return args;
}

void PrintMkbootimgArgs(const std::vector<std::string> &args,
                        bool null_separator) {
  if (null_separator) {
    for (const auto &arg : args) {
      std::cout << arg << '\0';
    }
    std::cout << std::flush;
  } else {
    for (const auto &arg : args) {
      std::cout << (arg.find(' ') != std::string::npos ? "'" + arg + "'" : arg)
                << " ";
    }
    std::cout << "\n";
  }
}

int main(int argc, char *argv[]) {
  auto maybe_args = ParseArguments(argc, argv);
  if (!maybe_args)
    return EXIT_FAILURE;
  auto &args = *maybe_args;

  std::ifstream input(args.boot_img, std::ios::binary);
  if (!input) {
    std::cerr << "Failed to open boot image: " << args.boot_img << "\n";
    return EXIT_FAILURE;
  }

  // Read magic number
  char magic[8];
  if (!input.read(magic, sizeof(magic))) {
    std::cerr << "Failed to read boot magic\n";
    return EXIT_FAILURE;
  }
  input.seekg(0);

  std::optional<BootImageInfo> boot_info;
  std::optional<VendorBootImageInfo> vendor_boot_info;

  if (std::string_view(magic, 8) == "ANDROID!") {
    boot_info = UnpackBootImage(input, args.output_dir);
  } else if (std::string_view(magic, 8) == "VNDRBOOT") {
    vendor_boot_info = UnpackVendorBootImage(input, args.output_dir);
  } else {
    std::cerr << "Invalid boot magic: " << std::string_view(magic, 8) << "\n";
    return EXIT_FAILURE;
  }

  if (!boot_info && !vendor_boot_info) {
    std::cerr << "Failed to unpack boot image\n";
    return EXIT_FAILURE;
  }

  if (args.format == "info") {
    if (boot_info) {
      std::cout << FormatPrettyText(*boot_info) << "\n";
    } else if (vendor_boot_info) {
      std::cout << FormatPrettyText(*vendor_boot_info) << "\n";
    }
  } else if (args.format == "mkbootimg") {
    if (boot_info) {
      PrintMkbootimgArgs(FormatMkbootimgArguments(*boot_info),
                         args.null_separator);
    } else if (vendor_boot_info) {
      PrintMkbootimgArgs(FormatMkbootimgArguments(*vendor_boot_info),
                         args.null_separator);
    }
  }

  return EXIT_SUCCESS;
}
