#include <neo/tar/ustar.hpp>

#include <neo/as_buffer.hpp>
#include <neo/platform.hpp>

#include <fstream>

namespace fs = std::filesystem;

using namespace std::literals;

#if NEO_OS_IS_WINDOWS
#include <windows.h>

namespace {

auto get_file_unix_mtime(const fs::path& fpath) {
    // Encode using wide chars:
    auto fpath_str = fpath.wstring();
    // Open the file handle for read access:
    auto handle = ::CreateFileW(fpath_str.data(),
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::system_error(std::error_code(::GetLastError(), std::system_category()),
                                "Unable to open file for read access [" + fpath.string() + "]");
    }

    ::FILETIME last_write_time{};
    bool       got_time_okay = ::GetFileTime(handle, nullptr, nullptr, &last_write_time);
    auto       got_time_err  = ::GetLastError();
    ::CloseHandle(handle);

    if (!got_time_okay) {
        throw std::system_error(std::error_code(got_time_err, std::system_category()),
                                "Unable to query file modification time [" + fpath.string() + "]");
    }

    // The unix time relative to Window's time format:
    const static std::int64_t unix_time_start      = 0x019db1ded53e8000;
    const static auto         win_ticks_per_escond = 10'000'000;
    // Windows time as an u64:
    const std::uint64_t win_mtime
        = (static_cast<std::uint64_t>(last_write_time.dwHighDateTime) << 32)
        | last_write_time.dwLowDateTime;
    return (win_mtime - unix_time_start) / win_ticks_per_second;
}

}  // namespace
#elif NEO_OS_IS_UNIX_LIKE
#include <sys/stat.h>
namespace {

auto get_file_unix_mtime(const fs::path& fpath) {
    struct ::stat status {};
    auto          rc = ::stat(fpath.c_str(), &status);
    if (rc != 0) {
        throw std::system_error(std::error_code(errno, std::system_category()),
                                "Failed to stat() to obtain file's mtime [" + fpath.string() + "]");
    }
    return status.st_mtime;
}

}  // namespace
#else
#error "We're not sure how to compile for this platform. Please submit a GitHub issue."
#endif

void neo::detail::ustar_writer_base::add_file(std::string_view dest, const fs::path& filepath) {
    fs::directory_entry info{filepath};

    ustar_member_info mem;
    mem.mtime = get_file_unix_mtime(filepath);

    if (dest.length() < mem.filename_bytes.size()) {
        mem.set_filename(dest);
    } else {
        auto last_dirsep = dest.rfind('/');
        if (last_dirsep == dest.npos) {
            throw std::runtime_error(
                "neo-compress does not (yet) support tar "
                "archive members with long filenames. (Processing file "s
                + filepath.string() + ")");
        }
        auto prefix = dest.substr(0, last_dirsep);
        auto fname  = dest.substr(last_dirsep + 1);
        if (fname.length() > mem.filename_bytes.size()) {
            throw std::runtime_error(
                "neo-compress does not (yet) support tar "
                "archive members with long filenames. (Processing file "s
                + filepath.string() + ")");
        }
        if (prefix.length() > mem.prefix_bytes.size()) {
            throw std::runtime_error(
                "neo-compress does not (yet) support tar "
                "archive members with long file paths. (Processing file "s
                + filepath.string() + ")");
        }
        mem.set_prefix(prefix);
        mem.set_filename(fname);
    }

    if (info.is_directory()) {
        mem.mode     = 0b111'111'101;
        mem.typeflag = mem.directory;
        write_member_header(mem);
        finish_member();
        return;
    }

    if (info.is_symlink()) {
        auto target = fs::read_symlink(filepath).string();
        if (target.length() > mem.linkname_bytes.size()) {
            throw std::runtime_error(
                "Unable to represent the given symbolic link (from ["s + filepath.string()
                + "]). The link target path is too long to handle (Target is [" + target + "]).");
        }
        mem.set_linkname(target);
        mem.typeflag = mem.symlink;
        write_member_header(mem);
        finish_member();
        return;
    }

    if (!info.is_regular_file()) {
        throw std::runtime_error(
            "neo-compress does not yet know how to properly add one of the input files to a Tar archive. Please submit an issue report! (File is at ["s
            + filepath.string() + "]");
    }

    mem.size = info.file_size();
    write_member_header(mem);

    std::ifstream infile;
    infile.exceptions(infile.exceptions() | std::ios::failbit | std::ios::badbit);
    infile.open(filepath, std::ios::binary);

    while (1) {
        thread_local std::array<char, 1024 * 2> buffer;
        auto n_read = infile.readsome(buffer.data(), buffer.size());
        if (n_read == 0) {
            break;
        }
        write_member_data(neo::as_buffer(buffer, n_read));
    }

    finish_member();
}
