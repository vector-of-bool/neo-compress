#pragma once

#include <filesystem>
#include <iosfwd>
#include <string_view>

namespace neo {

void compress_directory_targz(const std::filesystem::path& directory,
                              const std::filesystem::path& targz_destination);

struct expand_options {
    std::filesystem::path destination_directory;
    std::string_view      input_name;
    unsigned              strip_components = 0;
};

void expand_directory_targz(const expand_options& opts, std::istream& input);

void expand_directory_targz(const expand_options& opts, const std::filesystem::path& targz_input);

inline void expand_directory_targz(const std::filesystem::path& destination,
                                   const std::filesystem::path& targz_input) {
    return expand_directory_targz(
        expand_options{
            .destination_directory = destination,
            .input_name            = targz_input.string(),
        },
        targz_input);
}

}  // namespace neo
