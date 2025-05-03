#include "bootimg.h"
#include "utils.hpp"
#include "vendorbootimg.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

class ArgumentError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class HelpRequested : public std::exception {
public:
  const char *what() const noexcept override {
    return "Help requested by user.";
  }
};

struct ProgramArgs {
  fs::path boot_img;
  fs::path output_dir = "out";
  std::string format = "info";
  bool null_separator = false;
};

std::string_view TrimOuterQuotes(std::string_view str) {
  if (str.length() >= 2) {
    char first = str.front();
    char last = str.back();
    return ((first == '"' && last == '"') || (first == '\'' && last == '\''))
               ? str.substr(1, str.length() - 2)
               : str;
  }
  return str;
}

void PrintHelp() {
  std::cout << R"(unpackbootimg - Unpack boot, recovery, or vendor_boot images.

Usage:
  unpackbootimg --boot_img <image_path> [options]

Required:
  --boot_img <path>      Path to the input boot/recovery/vendor_boot image.

Options:
  -o, --out, --output <dir> Specify the output directory (default: "out").
                          Can use --output=dir or -o=dir format.
  --format <type>        Output format: 'info' (human-readable) or 'mkbootimg' (args for mkbootimg).
                          Default: 'info'. Can use --format=type.
  -0, --null             Use NULL character ('\0') as separator for mkbootimg format output.
  -h, --help             Show this help message and exit gracefully.

Example:
  unpackbootimg --boot_img boot.img -o=extracted_files --format=mkbootimg
  unpackbootimg --boot_img vendor_boot.img --output "my output dir"
)" << std::endl;
  throw HelpRequested();
}

ProgramArgs ParseArguments(int argc, char *argv[]) {
  ProgramArgs args;
  bool boot_img_provided = false;

  for (int i = 1; i < argc; ++i) {
    std::string_view current_arg = argv[i];
    std::string_view option_name = current_arg;
    std::optional<std::string_view> value_opt;

    size_t equals_pos = current_arg.find('=');
    if (equals_pos != std::string_view::npos) {
      option_name = current_arg.substr(0, equals_pos);
      value_opt = current_arg.substr(equals_pos + 1);
    }

    if (option_name == "-h" || option_name == "--help") {
      PrintHelp();
    } else if (option_name == "-0" || option_name == "--null") {
      if (value_opt)
        throw ArgumentError("Flag " + std::string(option_name) +
                            " does not take a value.");
      args.null_separator = true;
      continue;
    }

    bool needs_value = (option_name == "--boot_img" || option_name == "-o" ||
                        option_name == "--out" || option_name == "--output" ||
                        option_name == "--format");

    if (needs_value) {
      if (!value_opt) {
        if (++i >= argc) {
          throw ArgumentError("Missing value for argument: " +
                              std::string(option_name));
        }
        value_opt = argv[i];
      }

      std::string_view value = TrimOuterQuotes(*value_opt);

      if (option_name == "--boot_img") {
        args.boot_img = value;
        boot_img_provided = true;
      } else if (option_name == "-o" || option_name == "--out" ||
                 option_name == "--output") {
        args.output_dir = value;
      } else if (option_name == "--format") {
        args.format = value;
        if (args.format != "info" && args.format != "mkbootimg") {
          throw ArgumentError("Invalid format specified: '" + args.format +
                              "'. Use 'info' or 'mkbootimg'.");
        }
      }
    } else {
      throw ArgumentError("Unknown argument or unexpected value: " +
                          std::string(current_arg));
    }
  }

  if (!boot_img_provided) {
    throw ArgumentError("Missing required argument: --boot_img");
  }

  if (args.boot_img.empty()) {
    throw ArgumentError("Boot image path cannot be empty.");
  }

  std::error_code ec;
  if (!fs::exists(args.boot_img, ec)) {
    throw ArgumentError("Boot image file not found or inaccessible: " +
                        args.boot_img.string());
  }
  if (!fs::is_regular_file(args.boot_img, ec)) {
    throw ArgumentError("Specified boot image path is not a regular file: " +
                        args.boot_img.string());
  }

  return args;
}

void PrintMkbootimgArgs(const std::vector<std::string> &cmd_args,
                        bool null_separator) {
  if (cmd_args.empty())
    return;

  null_separator ? [&]() {
    for (const auto &arg : cmd_args) {
      std::cout << arg << '\0';
    }
  }()
                 : [&]() {
                     for (size_t i = 0; i < cmd_args.size(); ++i) {
                       const auto &arg = cmd_args[i];
                       bool needs_quotes = arg.find(' ') != std::string::npos;
                       std::cout << (needs_quotes ? "\"" + arg + "\"" : arg);
                       if (i < cmd_args.size() - 1) {
                         std::cout << " ";
                       }
                     }
                     std::cout << std::endl;
                   }();

  std::flush(std::cout);
}

int main(int argc, char *argv[]) {
  try {
    if (argc < 2) {
      PrintHelp();
    }

    const ProgramArgs args = ParseArguments(argc, argv);

    std::ifstream input(args.boot_img, std::ios::binary);
    if (!input) {
      throw std::runtime_error("Failed to open boot image: " +
                               args.boot_img.string());
    }

    constexpr size_t magic_size = 8;
    char magic[magic_size];
    if (!input.read(magic, magic_size)) {
      throw std::runtime_error("Failed to read magic from boot image: " +
                               args.boot_img.string());
    }
    input.seekg(0, std::ios::beg);

    std::variant<std::monostate, BootImageInfo, VendorBootImageInfo> image_info;
    std::string_view magic_view(magic, magic_size);

    if (magic_view == "ANDROID!") {
      image_info = UnpackBootImage(input, args.output_dir);
    } else if (magic_view == "VNDRBOOT") {
      image_info = UnpackVendorBootImage(input, args.output_dir);
    } else {
      std::string magic_str;
      for (size_t i = 0; i < magic_size; ++i) {
        char c = magic[i];
        magic_str += (isprint(static_cast<unsigned char>(c)) ? c : '.');
      }
      throw std::runtime_error("Invalid boot image magic: '" + magic_str + "'");
    }

    if (args.format == "info") {
      std::visit(
          [](const auto &info) {
            if constexpr (!std::is_same_v<std::decay_t<decltype(info)>,
                                          std::monostate>) {
              std::cout << FormatPrettyText(info);
            }
          },
          image_info);
    } else if (args.format == "mkbootimg") {
      std::visit(
          [&args](const auto &info) {
            if constexpr (!std::is_same_v<std::decay_t<decltype(info)>,
                                          std::monostate>) {
              PrintMkbootimgArgs(FormatMkbootimgArguments(info),
                                 args.null_separator);
            }
          },
          image_info);
    }

    return EXIT_SUCCESS;

  } catch (const HelpRequested &) {
    return EXIT_SUCCESS;
  } catch (const ArgumentError &e) {
    std::cerr << "Argument Error: " << e.what() << std::endl;
    std::cerr << "Use -h or --help for usage instructions." << std::endl;
    return EXIT_FAILURE;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (...) {
    std::cerr << "An unexpected error occurred." << std::endl;
    return EXIT_FAILURE;
  }
}