#pragma once

#include <filesystem>
#include <iosfwd>
#include <string_view>

namespace neo {

void compress_directory_targz(const std::filesystem::path& directory,
                              const std::filesystem::path& targz_destination);

void expand_directory_targz(const std::filesystem::path& directory,
                            const std::filesystem::path& targz_input);

void expand_directory_targz(const std::filesystem::path& directory,
                            std::istream&                input,
                            std::string_view             input_name);

}  // namespace neo
