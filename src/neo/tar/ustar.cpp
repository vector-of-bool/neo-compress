#include <neo/tar/ustar.hpp>

#include <neo/as_buffer.hpp>
#include <neo/iostream_io.hpp>
#include <neo/platform.hpp>

#include <fstream>

namespace fs = std::filesystem;

using namespace neo;

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
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
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
    const static auto         win_ticks_per_second = 10'000'000;
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

    mem.size     = info.file_size();
    mem.typeflag = mem.regular_file;
    write_member_header(mem);

    std::ifstream infile;
    infile.exceptions(infile.exceptions() | std::ios::badbit);
    infile.open(filepath, std::ios::binary);

    while (1) {
        thread_local std::array<char, 1024 * 1024 * 4> buffer;
        auto n_read = buffer_ios_read(infile, neo::as_buffer(buffer));
        if (n_read == 0) {
            break;
        }
        write_member_data(neo::as_buffer(buffer, n_read));
    }

    finish_member();
}

ustar_header_decoder::result ustar_header_decoder::operator()(const_buffer cb) {
    const auto n_read = buffer_copy(trivial_buffer(_raw) + _n_read_raw, cb);
    _n_read_raw += n_read;

    if (_n_read_raw < sizeof(_raw)) {
        return {.bytes_read = n_read};
    }

    _n_read_raw = 0;

    // If the magic number is all null, assume that we have read the final record.
    // TODO: Validate that the stream ends with two nul-blocks
    if (_raw.magic_ver == detail::null_tar_magic_ver) {
        return {.bytes_read = n_read, .done = true};
    }

    // Respect GNU or POSIX tar magic/version headers
    if (_raw.magic_ver != detail::gnu_tar_magic_ver
        && _raw.magic_ver != detail::posix_tar_magic_ver) {
        throw std::runtime_error("Invalid magic number in tar archive");
    }

    auto as_integer = [](const auto& str) -> std::uint64_t {
        std::uint64_t ret = 0;
        if (str[0] == '\x00') {
            return 0;
        }
        auto res = std::from_chars(str.data(), str.data() + str.size(), ret, 8);
        if (res.ec != std::errc{}) {
            throw std::runtime_error("Invalid integral string in archive member header");
        }
        return ret;
    };

    _value = {
        .filename_bytes = _raw.filename,
        .mode           = static_cast<int>(as_integer(_raw.mode)),
        .uid            = static_cast<int>(as_integer(_raw.uid)),
        .gid            = static_cast<int>(as_integer(_raw.gid)),
        .size           = as_integer(_raw.size),
        .mtime          = as_integer(_raw.mtime),
        .typeflag       = static_cast<ustar_member_info::type_t>(_raw.typeflag[0]),
        .linkname_bytes = _raw.linkname,
        .uname_bytes    = _raw.uname,
        .gname_bytes    = _raw.gname,
        .devmajor       = static_cast<int>(as_integer(_raw.devmajor)),
        .devminor       = static_cast<int>(as_integer(_raw.devminor)),
        .prefix_bytes   = _raw.prefix,
    };

    // Prepare the read the next member:
    _n_read_raw = 0;
    return {.bytes_read = n_read, ._val_ptr = &_value};
}

ustar_header_encoder::result
ustar_header_encoder::operator()(mutable_buffer out, const ustar_member_info& info) noexcept {
    if (_n_written_raw == 0) {
        _raw = {
            .filename = info.filename_bytes,
            .typeflag = {static_cast<char>(info.typeflag)},
            .linkname = info.linkname_bytes,
            .uname    = info.uname_bytes,
            .gname    = info.gname_bytes,
            .prefix   = info.prefix_bytes,
        };

        auto put_oct_num = [](auto& out_bytes, auto num) {
            out_bytes.back() = '\x00';
            auto res
                = std::to_chars(out_bytes.data(), out_bytes.data() + out_bytes.size() - 1, num, 8);
            neo_assert(invariant,
                       res.ec == std::errc{},
                       "Failed to convert an octal number in the ustar header",
                       int(res.ec),
                       num);
            auto n_chars    = res.ptr - out_bytes.data();
            auto dest_first = out_bytes.data() + out_bytes.size() - 1 - n_chars;
            buffer_copy(mutable_buffer(byte_pointer(dest_first), n_chars), as_buffer(out_bytes));
            for (auto z_ptr = out_bytes.data(); z_ptr != dest_first; ++z_ptr) {
                *z_ptr = '0';
            }
        };
        put_oct_num(_raw.mode, info.mode);
        put_oct_num(_raw.uid, info.uid);
        put_oct_num(_raw.gid, info.gid);
        put_oct_num(_raw.size, info.size);
        put_oct_num(_raw.mtime, info.mtime);
        put_oct_num(_raw.devmajor, info.devmajor);
        put_oct_num(_raw.devminor, info.devminor);

        auto          raw_buf = trivial_buffer(_raw);
        std::uint64_t chksum  = 0;
        for (auto p = raw_buf.data(); p != raw_buf.data_end(); ++p) {
            chksum += static_cast<int>(*p);
        }
        put_oct_num(_raw.chksum, chksum);
    }
    auto cbuf      = trivial_buffer(_raw) + _n_written_raw;
    auto n_written = buffer_copy(out, cbuf);
    _n_written_raw += n_written;
    result ret{.bytes_written = n_written, .done_ = n_written == cbuf.size()};
    if (ret.done()) {
        _n_written_raw = 0;
    }
    return ret;
}