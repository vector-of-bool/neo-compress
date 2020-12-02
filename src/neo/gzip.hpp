#pragma once

#include <neo/compress.hpp>
#include <neo/crc32.hpp>
#include <neo/decompress.hpp>

#include <neo/assert.hpp>
#include <neo/buffer_algorithm/copy.hpp>
#include <neo/buffer_sink.hpp>
#include <neo/buffer_source.hpp>
#include <neo/const_buffer.hpp>
#include <neo/ref.hpp>
#include <neo/switch_coro.hpp>

#include <stdexcept>

namespace neo {

/**
 * A gzip_compressor compresses a stream as a gzip stream, using `InnerCompressor`
 * to compress the actual body data.
 */
template <compressor_algorithm InnerCompressor>
class gzip_compressor {
    [[no_unique_address]] wrap_refs_t<InnerCompressor> _compressor;

    // Magic [0x1f, 0x8b] compresstion type DEFLATE [0x08]
    static inline const_buffer _fixed_header = const_buffer{"\x1f\x8b\x08"};
    // We don't support mtime yet
    static inline const_buffer _mtime = const_buffer{"\xde\xad\xbe\xef"};

    const_buffer  _header_buf = _fixed_header;
    const_buffer  _mtime_buf  = _mtime;
    crc32         _crc;
    std::uint32_t _size                   = 0;
    std::size_t   _num_crc_bytes_written  = 0;
    std::size_t   _num_size_bytes_written = 0;
    int           _coro                   = 0;

    /**
     * Copy `cb` into `out`, advancing `cb` and `out` by the number of bytes copied. Returns
     * `true` if the entire buffer was copied.
     */
    static constexpr bool _put_buf(mutable_buffer& out, const_buffer& cb) noexcept {
        const auto n_copied = buffer_copy(out, cb);
        out += n_copied;
        cb += n_copied;
        return cb.empty();
    }

    /**
     * Depending on the given bool, write a byte into the buffer. Sets the output
     * bool if the byte was written.
     */
    static constexpr bool
    _put_byte(bool& already_written, mutable_buffer& out, std::byte b) noexcept {
        if (already_written) {
            return true;
        }
        if (!out.empty()) {
            out[0] = b;
            out += 1;
            already_written = true;
            return true;
        }
        return false;
    }

public:
    constexpr gzip_compressor() = default;
    constexpr explicit gzip_compressor(InnerCompressor&& c)
        : _compressor(NEO_FWD(c)) {}

    constexpr void reset() noexcept { *this = gzip_compressor(); }

/**
 * Write the entire contents of `Buf` into `Dest`
 */
#define FLUSH_BUF(Dest, Buf)                                                                       \
    NEO_FN_MACRO_BEGIN                                                                             \
    while (!Buf.empty()) {                                                                         \
        if (Dest.empty()) {                                                                        \
            NEO_CORO_YIELD(calc_ret());                                                            \
        }                                                                                          \
        auto n_copied = buffer_copy(Dest, Buf);                                                    \
        Buf += n_copied;                                                                           \
        Dest += n_copied;                                                                          \
    }                                                                                              \
    NEO_FN_MACRO_END

#define PUT_BYTE(Out, Byte)                                                                        \
    NEO_FN_MACRO_BEGIN                                                                             \
    if (Out.empty()) {                                                                             \
        NEO_CORO_YIELD(calc_ret());                                                                \
    }                                                                                              \
    Out[0] = Byte;                                                                                 \
    Out += 1;                                                                                      \
    NEO_FN_MACRO_END

    constexpr compress_result
    operator()(mutable_buffer out, const_buffer in, flush f = flush::no_flush) noexcept {
        neo_assert(expects,
                   !NEO_CORO_IS_FINISHED(_coro),
                   "Application reused gzip_compressor without calling .reset()");

        // Create a compression result based on how much progress we have made
        auto calc_ret = [&, in_size = in.size(), out_size = out.size()] {
            return compress_result{
                .bytes_written = out_size - out.size(),
                .bytes_read    = in_size - in.size(),
                .done          = NEO_CORO_IS_FINISHED(_coro),
            };
        };

        bool compress_done = false;

        NEO_CORO_BEGIN(_coro);

        // Flush the header
        FLUSH_BUF(out, _header_buf);
        // The flags byte (all zero, for now)
        PUT_BYTE(out, std::byte(0x00));
        // Flush the mtime bytes
        FLUSH_BUF(out, _mtime_buf);
        // No interesting extra flags:
        PUT_BYTE(out, std::byte(0x00));
        // Flush the OS (0xff == unknown)
        PUT_BYTE(out, std::byte(0xff));

        // Write the actual body of data
        while (true) {
            {
                // Compress some data into `out`
                using std::as_const;
                auto compress_res = unref(_compressor)(as_const(out), as_const(in), f);
                // Update the running CRC
                _crc.feed(in.first(compress_res.bytes_read));
                // Update the running size count
                _size += static_cast<std::uint32_t>(compress_res.bytes_read);
                // Advance our buffers by the used space
                out += compress_res.bytes_written;
                in += compress_res.bytes_read;
                compress_done = compress_res.done;
            }

            if (!compress_done) {
                // We aren't done compressing data and the user has not yet flushed
                neo_assert(invariant,
                           out.empty() || in.empty(),
                           "Compressor did not exhaust the input nor output buffers",
                           out.size(),
                           in.size());
                NEO_CORO_YIELD(calc_ret());
                continue;
            }

            if (compress_done) {
                neo_assert_always(invariant,
                                  ((f & neo::flush::finish) == neo::flush::finish),
                                  "Compressor finished prematurely?",
                                  int(f));
                neo_assert_always(invariant,
                                  in.empty(),
                                  "Compressor did not take all of the data",
                                  in.size());
            }
            // Compression is finished. Finish up the Gzip file
            break;
        }

        // Writing the trailer yeah!
        while (_num_crc_bytes_written < sizeof(_crc.value())) {
            if (out.empty()) {
                NEO_CORO_YIELD(calc_ret());
            }
            neo_assert(expects, in.empty(), "Compressor is not accepting more data", in.size());

            auto      crc_val      = _crc.value();
            std::byte crc_bytes[4] = {
                std::byte(crc_val),
                std::byte(crc_val >> 8),
                std::byte(crc_val >> 16),
                std::byte(crc_val >> 24),
            };
            auto crc_buf       = neo::const_buffer(crc_bytes);
            auto n_written_crc = buffer_copy(out, crc_buf + _num_crc_bytes_written);
            _num_crc_bytes_written += n_written_crc;
            out += n_written_crc;
        }

        while (_num_size_bytes_written < sizeof(_size)) {
            if (out.empty()) {
                NEO_CORO_YIELD(calc_ret());
            }
            neo_assert(expects, in.empty(), "Compressor is not accepting more data", in.size());
            std::byte size_bytes[4] = {
                std::byte(_size),
                std::byte(_size >> 8),
                std::byte(_size >> 16),
                std::byte(_size >> 24),
            };
            auto size_buf      = neo::const_buffer(size_bytes);
            auto n_written_len = buffer_copy(out, size_buf + _num_size_bytes_written);
            out += n_written_len;
            _num_size_bytes_written += n_written_len;
        }

        NEO_CORO_END;

#undef FLUSH_BUF
#undef PUT_BYTE

        return calc_ret();
    }
};

template <typename C>
gzip_compressor(C &&) -> gzip_compressor<C>;

template <decompressor_algorithm InnerDecompressor>
class gzip_decompressor {
    [[no_unique_address]] wrap_refs_t<InnerDecompressor> _decompress;

    int _coro = 0;

    template <std::size_t Len>
    struct arrbuf {
        std::array<std::byte, Len> bytes = {};
        mutable_buffer             buf{bytes};

        constexpr arrbuf() = default;
        constexpr arrbuf(const arrbuf& other)
            : bytes(other.bytes)
            , buf(as_buffer(bytes)) {}
    };

    arrbuf<2> _magic;

    std::byte _flags{0};
    std::byte _compression_method{};
    arrbuf<4> _mtime;
    std::byte _xfl{};
    std::byte _os{};

    arrbuf<2>        _xlen;
    arrbuf<1024 * 2> _fextra;
    arrbuf<1024>     _fname;
    arrbuf<256>      _comment;
    arrbuf<2>        _hcrc;
    arrbuf<4>        _stored_crc32;
    arrbuf<4>        _stored_size;

    std::size_t _actual_size = 0;
    crc32       _actual_crc;

    constexpr std::uint16_t _xlen_uint16() const noexcept {
        return std::uint16_t(
            (std::uint16_t(_xlen.bytes[0]) | std::uint16_t(std::uint16_t(_xlen.bytes[1]) << 8)));
    }

    constexpr std::uint32_t _stored_crc_uint32() const noexcept {
        return (std::uint32_t(_stored_crc32.bytes[0])            //
                | (std::uint32_t(_stored_crc32.bytes[1]) << 8)   //
                | (std::uint32_t(_stored_crc32.bytes[2]) << 16)  //
                | (std::uint32_t(_stored_crc32.bytes[3]) << 24));
    }

    constexpr std::uint32_t _stored_size_uint32() const noexcept {
        return (std::uint32_t(_stored_size.bytes[0])            //
                | (std::uint32_t(_stored_size.bytes[1]) << 8)   //
                | (std::uint32_t(_stored_size.bytes[2]) << 16)  //
                | (std::uint32_t(_stored_size.bytes[3]) << 24));
    }

    constexpr bool _ftext_set() const noexcept { return int(_flags) & 1; }
    constexpr bool _fhcrc_set() const noexcept { return int(_flags) & 1 << 1; }
    constexpr bool _fextra_set() const noexcept { return int(_flags) & 1 << 2; }
    constexpr bool _fname_set() const noexcept { return int(_flags) & 1 << 3; }
    constexpr bool _fcomment_set() const noexcept { return int(_flags) & 1 << 4; }

public:
    constexpr gzip_decompressor() = default;
    constexpr explicit gzip_decompressor(InnerDecompressor&& c)
        : _decompress(NEO_FWD(c)) {}

    constexpr void reset() noexcept { *this = gzip_decompressor(); }

/**
 * Continually read bytes into Arr until Arr is full
 */
#define CORO_READ_BUF(Arr, Buf)                                                                    \
    NEO_FN_MACRO_BEGIN                                                                             \
    while (!Arr.buf.empty()) {                                                                     \
        if (Buf.empty()) {                                                                         \
            /* There's no data from the input, but we need more */                                 \
            NEO_CORO_YIELD(calc_ret());                                                            \
        }                                                                                          \
        /* Copy some bytes */                                                                      \
        const auto n_read = buffer_copy(Arr.buf, Buf);                                             \
        /* Advance the buffers by the distance we copied */                                        \
        Arr.buf += n_read;                                                                         \
        Buf += n_read;                                                                             \
    }                                                                                              \
    NEO_FN_MACRO_END

/**
 * Read bytes until either Arr is full or we encounter a NUL byte
 */
#define CORO_READ_ZSTR(Arr, Buf)                                                                   \
    NEO_FN_MACRO_BEGIN                                                                             \
    while (true) {                                                                                 \
        while (!Arr.buf.empty() && !Buf.empty() && Buf[0] != std::byte(0)) {                       \
            /* Copy a byte that is non-zero */                                                     \
            Arr.buf[0] = Buf[0];                                                                   \
            Buf += 1;                                                                              \
            Arr.buf += 1;                                                                          \
        }                                                                                          \
        if (Arr.buf.empty()) {                                                                     \
            /* There's no more room in the destination buffer */                                   \
            break;                                                                                 \
        }                                                                                          \
        if (!Buf.empty() && Buf[0] == std::byte(0)) {                                              \
            /* The next byte is a zero. We're done. */                                             \
            Buf += 1;                                                                              \
            break;                                                                                 \
        }                                                                                          \
        /* We need more bytes */                                                                   \
        NEO_CORO_YIELD(calc_ret());                                                                \
    }                                                                                              \
    NEO_FN_MACRO_END

/**
 * Read a single byte from the input and store it in `Byte`
 */
#define CORO_READ_BYTE(Byte, Buf)                                                                  \
    NEO_FN_MACRO_BEGIN                                                                             \
    if (Buf.empty()) {                                                                             \
        NEO_CORO_YIELD(calc_ret());                                                                \
    }                                                                                              \
    neo_assert(invariant, !Buf.empty(), "Expected more bytes of input");                           \
    Byte = Buf[0];                                                                                 \
    Buf += 1;                                                                                      \
    NEO_FN_MACRO_END

    constexpr decompress_result operator()(mutable_buffer out, const_buffer in) {
        const auto out_init = out;
        const auto in_init  = in;
        auto       calc_ret = [&] {
            return decompress_result{
                .bytes_written = out_init.size() - out.size(),
                .bytes_read    = in_init.size() - in.size(),
                .done          = NEO_CORO_IS_FINISHED(_coro),
            };
        };

        if (NEO_CORO_IS_FINISHED(_coro)) {
            return calc_ret();
        }

        NEO_CORO_BEGIN(_coro);

        // Read the magic number
        CORO_READ_BUF(_magic, in);
        if (_magic.bytes[0] != std::byte(0x1f) || _magic.bytes[1] != std::byte(0x8b)) {
            // Invalid magic number
            throw std::runtime_error("Invalid gzip magic number");
        }

        // Read the various gzip header bits
        CORO_READ_BYTE(_compression_method, in);
        CORO_READ_BYTE(_flags, in);
        CORO_READ_BUF(_mtime, in);
        CORO_READ_BYTE(_xfl, in);
        CORO_READ_BYTE(_os, in);

        // Optional, fextra:
        if (_fextra_set()) {
            CORO_READ_BUF(_xlen, in);
            if (_xlen_uint16() > _fextra.buf.size()) {
                throw std::runtime_error("gzip xlen is larger than supported");
            }
            _fextra.buf = _fextra.buf.first(_xlen_uint16());
            CORO_READ_BUF(_fextra, in);
        }

        // Optional, filename:
        if (_fname_set()) {
            CORO_READ_ZSTR(_fname, in);
        }

        // Optional: file comment
        if (_fcomment_set()) {
            CORO_READ_ZSTR(_comment, in);
        }

        // Optional, a header CRC, although we don't actually validate this (yet)
        if (_fhcrc_set()) {
            CORO_READ_BUF(_hcrc, in);
        }

        // Decompress the actual body of the file:
        while (true) {
            {
                // Decompress more
                const auto decomp_res = unref(_decompress)(std::as_const(out), std::as_const(in));
                // Update the running CRC
                _actual_crc.feed(out.first(decomp_res.bytes_written));
                // Advance our buffers
                in += decomp_res.bytes_read;
                out += decomp_res.bytes_written;
                // Track how much we've read
                _actual_size += decomp_res.bytes_written;
                if (decomp_res.done) {
                    break;
                }
            }
            // We aren't done yet, so yield until we get more data
            NEO_CORO_YIELD(calc_ret());
        }

        // Read the trailing CRC and data size
        CORO_READ_BUF(_stored_crc32, in);
        CORO_READ_BUF(_stored_size, in);

        // Check that the CRC matches
        if (_actual_crc.value() != _stored_crc_uint32()) {
            throw std::runtime_error("CRC-32 check failed");
        }

        // And the data size:
        if (_actual_size != _stored_size_uint32()) {
            throw std::runtime_error("Data length mismatch");
        }

        NEO_CORO_END;

        return calc_ret();

#undef CORO_READ_BYTE
#undef CORO_READ_BUF
#undef CORO_READ_ZSTR
    }
};

template <decompressor_algorithm D>
explicit gzip_decompressor(D &&) -> gzip_decompressor<D>;

}  // namespace neo
