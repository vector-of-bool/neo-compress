#pragma once

#include <neo/assert.hpp>
#include <neo/buffer_algorithm/copy.hpp>
#include <neo/buffer_algorithm/decode.hpp>
#include <neo/buffer_algorithm/encode.hpp>
#include <neo/buffer_range.hpp>
#include <neo/dynbuf_io.hpp>

#include <neo/assert.hpp>
#include <neo/iterator_facade.hpp>
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
        // Extra headers defined by pax:
        pax_extended_record = 'x',
        pax_global_record   = 'g',
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
    // The header doesn't fill the entire block
    std::array<char, 12> _dummy = {};
};

static_assert(sizeof(ustar_member_header_raw) == ustar_block_size);

class ustar_writer_base {
public:
    virtual void          write_member_header(const ustar_member_info& info) = 0;
    virtual std::uint64_t write_member_data(const_buffer data)               = 0;
    virtual void          finish_member()                                    = 0;

    void add_file(std::string_view dest, const std::filesystem::path&);
};

}  // namespace detail

class ustar_header_decoder {
    detail::ustar_member_header_raw _raw;
    std::size_t                     _n_read_raw = 0;

    ustar_member_info _value{};

public:
    struct result {
        std::size_t        bytes_read = 0;
        bool               done       = false;
        ustar_member_info* _val_ptr   = nullptr;

        bool has_error() { return false; }
        bool has_value() { return done || _val_ptr; }

        auto& value() noexcept {
            neo_assert(expects,
                       _val_ptr,
                       "header-decode value access with incomplete decode result");
            return *_val_ptr;
        }
    };

    result operator()(const_buffer cb);
};

struct ustar_header_encoder {
    detail::ustar_member_header_raw _raw;
    std::size_t                     _n_written_raw = 0;

public:
    struct result {
        std::size_t    bytes_written = 0;
        bool           done_         = false;
        constexpr bool done() const noexcept { return done_; }
    };

    result operator()(mutable_buffer mb, const ustar_member_info&) noexcept;
};

template <buffer_source Input>
class ustar_reader {
    // The underlying data-reader:
    [[no_unique_address]] wrap_refs_t<Input> _input;

    std::uint64_t _remaining_member_size = 0;
    int           _trailing_member_nuls  = 0;

    ustar_header_decoder _header_decode;

    void _consume_remaining_member_data() {
        auto&         in           = input();
        std::uint64_t n_to_consume = _remaining_member_size + _trailing_member_nuls;
        while (n_to_consume) {
            auto&& ignore = in.next(n_to_consume);
            auto   n_got  = buffer_size(ignore);
            in.consume(n_got);
            n_to_consume -= n_got;
        }
        _remaining_member_size = 0;
        _trailing_member_nuls  = 0;
    }

public:
    explicit ustar_reader(Input&& in)
        : _input(NEO_FWD(in)) {}

    NEO_DECL_UNREF_GETTER(input, _input);

    std::optional<ustar_member_info> next_member() {
        // Skip any member data that is trailing
        _consume_remaining_member_data();

        // Get the header:
        auto decode_res = buffer_decode(_header_decode, input());
        if (!decode_res.has_value() || decode_res.done) {
            return std::nullopt;
        }

        auto meminfo        = decode_res.value();
        auto n_data_records = (meminfo.size + detail::ustar_block_size) / detail::ustar_block_size;

        _remaining_member_size = meminfo.size;
        _trailing_member_nuls
            = static_cast<int>((n_data_records * detail::ustar_block_size) - meminfo.size)
            % detail::ustar_block_size;

        return decode_res.value();
    }

    auto next(std::size_t max_size) noexcept {
        auto read_size = max_size > _remaining_member_size ? _remaining_member_size : max_size;
        return input().next(read_size);
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

    auto all_data() noexcept { return next(_remaining_member_size); }

    class member_iterator : public neo::iterator_facade<member_iterator> {
        ustar_reader*                    _reader = nullptr;
        std::optional<ustar_member_info> _info;

    public:
        constexpr member_iterator() = default;
        explicit member_iterator(ustar_reader& r)
            : _reader(&r)
            , _info(r.next_member()) {}

        struct sentinel_type {};

        const ustar_member_info& dereference() const noexcept {
            neo_assert(expects,
                       !at_end(),
                       "Dereferenced an end/invalid ustar_reader::member_iterator");
            return *_info;
        }

        void increment() noexcept { _info = _reader->next_member(); }

        constexpr bool at_end() const noexcept { return !_info.has_value(); }
    };

    auto begin() noexcept { return member_iterator{*this}; }
    auto end() const noexcept { return typename member_iterator::sentinel_type{}; }
};

template <typename T>
ustar_reader(T &&) -> ustar_reader<T>;

template <buffer_sink Output>
class ustar_writer : public detail::ustar_writer_base {
    [[no_unique_address]] wrap_refs_t<Output> _output;

    ustar_header_encoder _header_encode;

    std::uint64_t _member_data_written = 0;

    void _finish_member_data() {
        // A global block of zeros. Useful.
        static const std::array<std::byte, detail::ustar_block_size> zeros = {};
        // Write the correct number of zeroes to land the next header onto a good block
        auto n_zeros
            = (detail::ustar_block_size - (_member_data_written % detail::ustar_block_size))
            % detail::ustar_block_size;
        auto out = output().prepare(n_zeros);
        if (buffer_size(out) < n_zeros) {
            throw std::runtime_error(
                "Failed to write padding zeros in archive block following data member");
        }
        buffer_copy(out, as_buffer(zeros), n_zeros);
        output().commit(n_zeros);
        _member_data_written = 0;
    }

public:
    explicit ustar_writer(Output&& out)
        : _output(NEO_FWD(out)) {}

    NEO_DECL_UNREF_GETTER(output, _output);

    std::uint64_t write_member_data(const_buffer data) final {
        auto n_written = buffer_copy(output(), data);
        _member_data_written += n_written;
        return n_written;
    }

    template <buffer_input In>
    std::uint64_t write_member_data(In&& in) {
        auto n_written = buffer_copy(output(), in);
        _member_data_written += n_written;
        return n_written;
    }

    void write_member_header(const ustar_member_info& info) final {
        auto result = buffer_encode(_header_encode, output(), info);
        if (!result.done()) {
            throw std::runtime_error("Failed to write tar member header. Not enough room?");
        }
    }

    template <buffer_input Input>
    void write_member(const ustar_member_info& mem_info, Input&& in) {
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
        constexpr auto num_zeros        = detail::ustar_block_size * 2;
        std::byte      zeros[num_zeros] = {};
        const auto     n_written        = buffer_copy(output(), as_buffer(zeros));
        if (n_written != num_zeros) {
            throw std::runtime_error("Failed to write terminating zero blocks on tar archive");
        }
    }
};

template <typename T>
ustar_writer(T &&) -> ustar_writer<T>;

}  // namespace neo
