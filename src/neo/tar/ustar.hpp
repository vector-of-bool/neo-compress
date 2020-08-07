#pragma once

#include <neo/assert.hpp>
#include <neo/buffer_algorithm/copy.hpp>
#include <neo/buffer_range.hpp>
#include <neo/io_buffer.hpp>

#include <neo/assert.hpp>
#include <neo/ref.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>

namespace neo {

struct ustar_member_info {
    enum type_t : char {
        none,
        regular_file  = '0',
        link          = '1',
        symlink       = '2',
        char_special  = '3',
        block_special = '4',
        directory     = '5',
        fifo          = '6',
        hpc_file      = '7',
    };
    std::array<char, 100> filename_bytes = {};
    int                   mode           = 0b110'110'100;
    int                   uid            = 0;
    int                   gid            = 0;
    std::uint64_t         size           = 0;
    std::uint64_t         mtime          = 0;
    type_t                typeflag       = {};
    std::array<char, 100> linkname_bytes = {};
    std::array<char, 32>  uname_bytes    = {};
    std::array<char, 32>  gname_bytes    = {};
    int                   devmajor       = 0;
    int                   devminor       = 0;
    std::array<char, 155> prefix_bytes   = {};

    constexpr auto filename_str() const noexcept { return _as_string(filename_bytes); }
    constexpr auto set_filename(std::string_view s) noexcept { _set_str(filename_bytes, s); }
    constexpr auto prefix_str() const noexcept { return _as_string(prefix_bytes); }
    constexpr auto set_prefix(std::string_view s) noexcept { _set_str(prefix_bytes, s); }
    constexpr auto linkname_str() const noexcept { return _as_string(linkname_bytes); }
    constexpr void set_linkname(std::string_view s) noexcept { _set_str(linkname_bytes, s); }
    constexpr auto uname_str() const noexcept { return _as_string(uname_bytes); }
    constexpr void set_uname(std::string_view s) noexcept { _set_str(uname_bytes, s); }
    constexpr auto gname_str() const noexcept { return _as_string(gname_bytes); }
    constexpr void set_gname(std::string_view s) noexcept { _set_str(gname_bytes, s); }

    constexpr bool is_file() const noexcept { return is_regular_file() || is_hpc_file(); }
    constexpr bool is_regular_file() const noexcept { return typeflag == regular_file; }
    constexpr bool is_link() const noexcept { return typeflag == link; }
    constexpr bool is_symlink() const noexcept { return typeflag == symlink; }
    constexpr bool is_char_special() const noexcept { return typeflag == char_special; }
    constexpr bool is_block_special() const noexcept { return typeflag == block_special; }
    constexpr bool is_directory() const noexcept { return typeflag == directory; }
    constexpr bool is_fifo() const noexcept { return typeflag == fifo; }
    constexpr bool is_hpc_file() const noexcept { return typeflag == hpc_file; }

private:
    template <std::size_t N>
    static constexpr std::string_view _as_string(const std::array<char, N>& arr) noexcept {
        std::string_view full{arr.data(), arr.size()};
        // Create a string view until the first null byte, or the full length
        auto first_nul = full.find('\x00');
        return std::string_view(arr.data(), first_nul == full.npos ? arr.size() : first_nul);
    }

    template <std::size_t N>
    constexpr void _set_str(std::array<char, N>& arr, std::string_view s) noexcept {
        auto n_bytes = buffer_copy(as_buffer(arr), as_buffer(s));
        for (auto p = arr.begin() + n_bytes; p != arr.end(); ++p) {
            *p = '\x00';
        }
    }
};

namespace detail {

constexpr inline std::size_t         ustar_block_size = 512;
constexpr inline std::array<char, 8> gnu_tar_magic_ver{'u', 's', 't', 'a', 'r', ' ', ' ', '\x00'};
constexpr inline std::array<char, 8> posix_tar_magic_ver{'u', 's', 't', 'a', 'r', '\x00', '0', '0'};
constexpr inline std::array<char, 8> null_tar_magic_ver{};

struct ustar_member_header_raw {
    std::array<char, 100> filename  = {};
    std::array<char, 8>   mode      = {};
    std::array<char, 8>   uid       = {};
    std::array<char, 8>   gid       = {};
    std::array<char, 12>  size      = {};
    std::array<char, 12>  mtime     = {};
    std::array<char, 8>   chksum    = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    std::array<char, 1>   typeflag  = {};
    std::array<char, 100> linkname  = {};
    std::array<char, 8>   magic_ver = posix_tar_magic_ver;
    std::array<char, 32>  uname     = {};
    std::array<char, 32>  gname     = {};
    std::array<char, 8>   devmajor  = {};
    std::array<char, 8>   devminor  = {};
    std::array<char, 155> prefix    = {};
};

static_assert(sizeof(ustar_member_header_raw) == 345 + 155);

struct ustar_reader_base {
    std::uint64_t _remaining_member_size = 0;
    int           _trailing_member_nuls  = 0;

    template <buffer_range Bufs>
    ustar_member_header_raw _read_raw_header(Bufs&& b) noexcept {
        detail::ustar_member_header_raw raw;
        auto n_copied = buffer_copy(mutable_buffer(byte_pointer(&raw), sizeof raw), b);
        neo_assert(invariant,
                   n_copied == sizeof raw,
                   "Did not copy enough bytes into raw tar header",
                   n_copied);
        return raw;
    }

    std::optional<ustar_member_info> _process_raw_header(const ustar_member_header_raw& raw) {
        // If the magic number is all null, assume that we have read the final record.
        // TODO: Validate that the stream ends with two nul-blocks
        if (raw.magic_ver == null_tar_magic_ver) {
            return std::nullopt;
        }

        // Respect GNU or POSIX tar magic/version headers
        if (raw.magic_ver != detail::gnu_tar_magic_ver
            && raw.magic_ver != detail::posix_tar_magic_ver) {
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

        auto mem_size       = as_integer(raw.size);
        auto n_data_records = (mem_size + sizeof(raw)) / detail::ustar_block_size;

        _remaining_member_size = mem_size;
        _trailing_member_nuls
            = static_cast<int>((n_data_records * detail::ustar_block_size) - mem_size);

        return ustar_member_info{
            .filename_bytes = raw.filename,
            .mode           = static_cast<int>(as_integer(raw.mode)),
            .uid            = static_cast<int>(as_integer(raw.uid)),
            .gid            = static_cast<int>(as_integer(raw.gid)),
            .size           = mem_size,
            .mtime          = as_integer(raw.mtime),
            .typeflag       = static_cast<ustar_member_info::type_t>(raw.typeflag[0]),
            .linkname_bytes = raw.linkname,
            .uname_bytes    = raw.uname,
            .gname_bytes    = raw.gname,
            .devmajor       = static_cast<int>(as_integer(raw.devmajor)),
            .devminor       = static_cast<int>(as_integer(raw.devminor)),
            .prefix_bytes   = raw.prefix,
        };
    }
};

class ustar_writer_base {
public:
    virtual void          write_member_header(const ustar_member_info& info) = 0;
    virtual std::uint64_t write_member_data(const_buffer data)               = 0;
    virtual void          finish_member()                                    = 0;

    void add_file(std::string_view dest, const std::filesystem::path&);
};

}  // namespace detail

template <buffer_source Input>
class ustar_reader : detail::ustar_reader_base {
    // The underlying data-reader:
    [[no_unique_address]] wrap_if_reference_t<Input> _input;

    static_assert(sizeof(detail::ustar_member_header_raw) == (345 + 155));

public:
    explicit ustar_reader(Input&& in)
        : _input(NEO_FWD(in)) {}

    Input&       input() noexcept { return _input; }
    const Input& input() const noexcept { return _input; }

    std::optional<ustar_member_info> next_member() {
        // Skip any member data that is trailing
        input().consume(_remaining_member_size);
        input().consume(_trailing_member_nuls);

        // Read a block to contain the next header
        auto next_header = input().data(detail::ustar_block_size);
        // We must have read an entire block, or the header is incomplete
        if (buffer_size(next_header) < detail::ustar_block_size) {
            throw std::runtime_error(
                "Failed to read an entire data block from ustar archive. (Unexpected EOF?)");
        }

        // Copy the raw header information
        detail::ustar_member_header_raw raw;
        auto n_copied = buffer_copy(mutable_buffer(byte_pointer(&raw), sizeof raw), next_header);
        neo_assert(invariant,
                   n_copied == sizeof raw,
                   "Incorrect copy amout when reading in tar header",
                   n_copied);
        // Consume that data from the input
        input().consume(detail::ustar_block_size);
        // Process the header and return the member info
        return _process_raw_header(raw);
    }

    auto data(std::size_t max_size) noexcept {
        auto read_size = max_size > _remaining_member_size ? _remaining_member_size : max_size;
        return input().data(read_size);
    }

    void consume(std::size_t s) noexcept {
        neo_assert(expects,
                   s <= _remaining_member_size,
                   "Attempted to consume too many bytes from a ustar archive member",
                   s,
                   _remaining_member_size);
        _remaining_member_size -= s;
        input().consume(s);
    }

    auto all_data() noexcept { return data(_remaining_member_size); }
};

template <typename T>
ustar_reader(T &&) -> ustar_reader<T>;

template <buffer_sink Output>
class ustar_writer : public detail::ustar_writer_base {
    [[no_unique_address]] wrap_if_reference_t<Output> _output;

    std::uint64_t _member_data_written = 0;

    void _finish_member_data() {
        // A global block of zeros. Useful.
        static const std::array<std::byte, detail::ustar_block_size> zeros = {};
        // Write the correct number of zeroes to land the next header onto a good block
        auto n_zeros = detail::ustar_block_size - (_member_data_written % detail::ustar_block_size);
        auto out     = output().prepare(n_zeros);
        if (buffer_size(out) != n_zeros) {
            throw std::runtime_error(
                "Failed to write padding zeros in archive block following data member");
        }
        buffer_copy(out, as_buffer(zeros), n_zeros);
        output().commit(n_zeros);
        _member_data_written = 0;
    }

public:
    explicit ustar_writer(Output&& out)
        : _output(std::forward<Output>(out)) {}

    Output&       output() noexcept { return _output; }
    const Output& output() const noexcept { return _output; }

    std::uint64_t write_member_data(const_buffer data) final {
        auto size      = data.size();
        auto obuf      = output().prepare(size);
        auto n_written = buffer_copy(obuf, data);
        output().commit(n_written);
        _member_data_written += n_written;
        return n_written;
    }

    template <buffer_range Data>
    std::uint64_t write_member_data(Data&& data) {
        auto size      = buffer_size(data);
        auto obuf      = output().prepare(size);
        auto n_written = buffer_copy(obuf, data);
        output().commit(n_written);
        _member_data_written += n_written;
        return n_written;
    }

    template <buffer_source In>
    std::uint64_t write_member_data(In&& in) {
        std::uint64_t n_written_total = 0;
        while (true) {
            auto more      = in.data(1024);
            auto n_written = write_member_data(more);
            in.consume(n_written);
            n_written_total += n_written;
            if (n_written == 0) {
                break;
            }
        }
        return n_written_total;
    }

    void write_member_header(const ustar_member_info& info) final {
        detail::ustar_member_header_raw raw = {
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
        put_oct_num(raw.mode, info.mode);
        put_oct_num(raw.uid, info.uid);
        put_oct_num(raw.gid, info.gid);
        put_oct_num(raw.size, info.size);
        put_oct_num(raw.mtime, info.mtime);
        put_oct_num(raw.devmajor, info.devmajor);
        put_oct_num(raw.devminor, info.devminor);

        auto          raw_buf = trivial_buffer(raw);
        std::uint64_t chksum  = 0;
        for (auto p = raw_buf.data(); p != raw_buf.data_end(); ++p) {
            chksum += static_cast<int>(*p);
        }
        put_oct_num(raw.chksum, chksum);

        // Write the header
        auto out = output().prepare(sizeof raw);
        if (buffer_size(out) < sizeof raw) {
            throw std::runtime_error("Failed to prepare room for archive member header");
        }
        buffer_copy(out, raw_buf);
        output().commit(sizeof raw);

        // Write the zero padding
        static const std::array<char, detail::ustar_block_size - sizeof raw> zeros = {};

        out = output().prepare(sizeof zeros);
        if (buffer_size(out) < sizeof zeros) {
            throw std::runtime_error("Failed to prepare room for archive member header (padding)");
        }
        buffer_copy(out, as_buffer(zeros));
        output().commit(sizeof zeros);
    }

    template <typename Input>
    void write_member(const ustar_member_info& mem_info,
                      Input&&                  in)  //
        requires(buffer_source<Input> || buffer_range<Input>) {
        write_member_header(mem_info);
        auto n_written = write_member_data(in);
        neo_assert(invariant,
                   n_written == mem_info.size,
                   "Incorrect number of bytes written for archive member",
                   n_written,
                   mem_info.size);
        finish_member();
    }

    void finish_member() final { _finish_member_data(); }

    void finish() {
        finish_member();
        auto                      remaining_zeros = detail::ustar_block_size * 2;
        std::array<std::byte, 64> zeros           = {};
        while (remaining_zeros) {
            auto out       = output().prepare(remaining_zeros);
            auto n_written = buffer_copy(out, as_buffer(zeros));
            output().commit(n_written);
            remaining_zeros -= static_cast<int>(n_written);
        }
    }
};

template <typename T>
ustar_writer(T &&) -> ustar_writer<T>;

}  // namespace neo
