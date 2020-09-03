#include "./deflate.hpp"

#include <zlib.h>

#include <ostream>

#define MY_Z_STATE (*static_cast<::z_stream*>(_z_stream_ptr))

using namespace neo;

neo::deflate_compressor::deflate_compressor(deflate_compressor::allocator_type alloc) noexcept
    : compression_base(alloc) {
    ::deflateInit2(&MY_Z_STATE, 5, Z_DEFLATED, -12, 8, Z_DEFAULT_STRATEGY);
}

deflate_compressor::~deflate_compressor() {
    if (_z_stream_ptr) {
        ::deflateEnd(&MY_Z_STATE);
    }
}

void neo::deflate_compressor::reset() noexcept { ::deflateReset(&MY_Z_STATE); }

neo::compress_result
neo::deflate_compressor::operator()(neo::mutable_buffer out, neo::const_buffer in, neo::flush f) {
    neo::compress_result acc;

    ::z_stream& strm = MY_Z_STATE;
    strm.next_in     = const_cast<::Byte*>(reinterpret_cast<const ::Byte*>(in.data()));
    strm.avail_in    = static_cast<uInt>(in.size());
    strm.next_out    = reinterpret_cast<::Byte*>(out.data());
    strm.avail_out   = static_cast<uInt>(out.size());

    auto result = ::deflate(&strm, f == flush::finish ? Z_FINISH : Z_NO_FLUSH);
    neo_assert(invariant,
               result == Z_OK || result == Z_STREAM_END || result == Z_BUF_ERROR,
               "deflate() failed unexpectedly. ??",
               result,
               in.size(),
               out.size(),
               strm.avail_in,
               strm.avail_out);
    return {
        .bytes_written = out.size() - strm.avail_out,
        .bytes_read    = in.size() - strm.avail_in,
        .done          = result == Z_STREAM_END,
    };
}
