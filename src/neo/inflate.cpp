#include "./inflate.hpp"

#include <zlib.h>

using namespace std::literals;
using namespace neo;

#define MY_Z_STATE (*static_cast<::z_stream*>(_z_stream_ptr))

/**
 * Initialize the state for tinfl.
 */
neo::inflate_decompressor::inflate_decompressor(inflate_decompressor::allocator_type alloc) noexcept
    : compression_base(alloc) {
    ::inflateInit2(&MY_Z_STATE, -15);
}

inflate_decompressor::~inflate_decompressor() {
    if (_z_stream_ptr) {
        ::inflateEnd(&MY_Z_STATE);
    }
}

void neo::inflate_decompressor::reset() noexcept { ::inflateReset(&MY_Z_STATE); }

neo::decompress_result neo::inflate_decompressor::operator()(neo::mutable_buffer out,
                                                             neo::const_buffer   in) {

    ::z_stream& strm = MY_Z_STATE;
    strm.next_in     = const_cast<::Byte*>(reinterpret_cast<const ::Byte*>(in.data()));
    strm.avail_in    = static_cast<uInt>(in.size());
    strm.next_out    = reinterpret_cast<::Byte*>(out.data());
    strm.avail_out   = static_cast<uInt>(out.size());

    auto result = ::inflate(&strm, Z_NO_FLUSH);
    if (result != Z_OK && result != Z_BUF_ERROR && result != Z_STREAM_END) {
        // There was an error from tinfl!
        if (strm.msg) {
            throw std::runtime_error("Data inflate failed. Corrupted? Message from zlib: "s
                                     + strm.msg);
        } else {
            throw std::runtime_error("Data inflate failed. Corrupted?");
        }
    }
    return {
        .bytes_written = out.size() - strm.avail_out,
        .bytes_read    = in.size() - strm.avail_in,
        .done          = result == Z_STREAM_END,
    };
}
