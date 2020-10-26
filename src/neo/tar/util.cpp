#include "./util.hpp"

#include "../deflate.hpp"
#include "../gzip.hpp"
#include "../inflate.hpp"
#include "./ustar.hpp"

#include <neo/as_buffer.hpp>
#include <neo/iostream_io.hpp>
#include <neo/transform_io.hpp>
#include <neo/ufmt.hpp>

#include <fstream>
#include <string>

#if !NEO_OS_IS_WINDOWS
#include <sys/stat.h>
#endif

using namespace neo;

namespace fs = std::filesystem;

namespace {

void restore_permissions(const fs::path&          file,
                         const ustar_member_info& meminfo,
                         std::string_view         input_name,
                         const fs::path&          partpath);

#if !NEO_OS_IS_WINDOWS
void restore_permissions(const fs::path&          file,
                         const ustar_member_info& meminfo,
                         std::string_view         input_name,
                         const fs::path&          partpath) {
    auto rc = ::chmod(file.c_str(), static_cast<mode_t>(meminfo.mode));
    if (rc) {
        throw std::system_error(std::error_code(errno, std::system_category()),
                                "Failed to restore filemode for [" + file.string()
                                    + "], extracted from [" + partpath.string() + "] contained in ["
                                    + std::string(input_name) + "]");
    }
}
#endif

}  // namespace

void neo::compress_directory_targz(const fs::path& directory, const fs::path& targz_dest) {
    // Open the file for writing:
    std::ofstream out;
    out.exceptions(out.exceptions() | std::ios::badbit | std::ios::failbit);
    out.open(targz_dest, std::ios::binary);

    // Compressor state:
    gzip_compressor<deflate_compressor> gzip;
    // Compression pipeline:
    iostream_io  file_out{out};
    ustar_writer tar_writer{buffer_transform_sink{file_out, gzip}};

    auto abs_path = fs::canonical(directory);
    for (auto item : fs::recursive_directory_iterator(abs_path)) {
        auto relpath = item.path().lexically_relative(abs_path);
        tar_writer.add_file(relpath.string(), item.path());
    }

    tar_writer.finish();
    buffer_transform(gzip, file_out, const_buffer(), flush::finish);
}

/// XXX: Does not yet restore mtime/ownership
void neo::expand_directory_targz(const expand_options& opts, const fs::path& targz_source) {
    std::ifstream in;
    in.exceptions(in.exceptions() | std::ios::badbit);
    in.open(targz_source, std::ios::binary);

    expand_directory_targz(opts, in, targz_source.string());
}

void neo::expand_directory_targz(const expand_options& opts,
                                 std::istream&         in,
                                 std::string_view      input_name) {
    gzip_decompressor<inflate_decompressor> gzip;
    iostream_io                             file_in{in};
    buffer_transform_source                 gzip_in{file_in, gzip};

    ustar_reader tar_reader{gzip_in};

    auto& destination = opts.destination_directory;

    for (const auto& meminfo : tar_reader) {
        fs::path filepath = meminfo.filename_str();
        if (!meminfo.prefix_str().empty()) {
            filepath = meminfo.prefix_str() / filepath;
        }

        auto n_elems = std::distance(filepath.begin(), filepath.end());
        if (opts.strip_components >= n_elems) {
            continue;
        }

        auto norm = filepath.lexically_normal();
        if (norm.empty()) {
            throw std::runtime_error(
                ufmt("Archive [{}] contains member with an empty filename/filepath. The "
                     "archive may be malformed or created abnormally.",
                     input_name));
        }
        if (norm.is_absolute()) {
            // Pretty ugly... want std::format...
            throw std::runtime_error(
                ufmt("Archive [{}] contains a member with an absolute path. The archive is unsafe "
                     "to extract. If may be malformed, craeted abnormally, or is malicious. Member "
                     "filename is [{}], prefix is [{}]. Normalized filepath is [{}].",
                     input_name,
                     meminfo.filename_str(),
                     meminfo.prefix_str(),
                     norm.string()));
        }
        if (norm.begin()->string() == "..") {
            // Pretty ugly... want std::format...
            throw std::runtime_error(
                ufmt("Archive [{}] contains member which would extract above the destination path. "
                     "The archive is unsafe to extract. It may be malformed, created abnormally, "
                     "or malicious. Member filename is [{}], prefix is [{}]. Normalized filename "
                     "is [{}]. Destination directory is [{}], which would resolve to [{}].",
                     input_name,
                     meminfo.filename_str(),
                     meminfo.prefix_str(),
                     norm.string(),
                     destination.string(),
                     (destination / norm).lexically_normal().string()));
        }

        auto stripped_path = std::reduce(std::next(filepath.begin(), opts.strip_components),
                                         filepath.end(),
                                         std::filesystem::path(),
                                         std::divides{});
        auto file_dest     = (destination / stripped_path).lexically_normal();

        if (meminfo.is_directory()) {
            fs::create_directory(file_dest);
        } else if (meminfo.is_symlink()) {
            fs::create_symlink(meminfo.linkname_str(), file_dest);
        } else if (meminfo.is_link()) {
            fs::create_hard_link(meminfo.linkname_str(), file_dest);
        } else if (meminfo.is_file()) {
            std::ofstream ofile;
            ofile.exceptions(ofile.exceptions() | std::ios::badbit | std::ios::failbit);
            errno = 0;
            try {
                ofile.open(file_dest, std::ios::binary);
                neo::iostream_io data_sink{ofile};
                buffer_copy(data_sink, tar_reader.all_data());
            } catch (const std::system_error& e) {
                throw std::
                    system_error(std::error_code(errno, std::generic_category()),
                                 neo::ufmt("Failure while extractive archive member to [{}]: {}",
                                           file_dest.string(),
                                           e.what()));
            }
            ofile.close();
            if constexpr (!neo::os_is_windows) {
                restore_permissions(file_dest, meminfo, input_name, norm);
            }
        } else if (meminfo.typeflag == ustar_member_info::type_t::pax_extended_record
                   || meminfo.typeflag == ustar_member_info::type_t::pax_global_record) {
            // TODO: We don't handle pax headers anything special yet.
        } else {
            throw std::runtime_error(
                neo::ufmt("Don't know how to expand archive member. Archive "
                          "is [{}], member is [{}], type is [{}].",
                          input_name,
                          filepath.string(),
                          char(meminfo.typeflag)));
        }
    }
}
