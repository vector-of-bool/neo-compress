#include "./deflate.hpp"

#include <neo/c/miniz/miniz.h>

#define MY_STATE reinterpret_cast<::tdefl_compressor&>(_state_bytes)

neo::deflate_compressor::deflate_compressor() noexcept {
    static_assert(sizeof(_state_bytes) == sizeof(::tdefl_compressor));
    auto st = new (&MY_STATE)::tdefl_compressor{};
    auto rc = ::tdefl_init(st, nullptr, nullptr, 0);
    assert(rc == TDEFL_STATUS_OKAY);
}

void neo::deflate_compressor::reset() noexcept {
    auto rc = ::tdefl_init(&MY_STATE, nullptr, nullptr, 0);
    assert(rc == TDEFL_STATUS_OKAY);
}

neo::compress_result
neo::deflate_compressor::operator()(neo::mutable_buffer out, neo::const_buffer in, neo::flush f) {
    neo::compress_result acc;

    while (true) {
        auto in_size  = in.size();
        auto out_size = out.size();
        // Do the actual compression
        auto result = ::tdefl_compress(&MY_STATE,
                                       in.data(),
                                       &in_size,
                                       out.data(),
                                       &out_size,
                                       f == flush::finish  //
                                           ? TDEFL_FINISH
                                           : TDEFL_NO_FLUSH);
        acc += {
            .bytes_written = out_size,
            .bytes_read    = in_size,
            .done          = result == TDEFL_STATUS_DONE,
        };
        in += in_size;
        out += out_size;
        if (in.empty() || out.empty()) {
            return acc;
        }
        // tdefl didn't consume _all_ of the buffers, but it *must* have made
        // some progress. (Otherwise we'll enter an infinite loop.)
        assert((in_size != 0 || out_size != 0) &&
               "neo::deflate_compressor compressor entered a bad state! This program will "
               "now halt. (It would otherwise enter an infinite loop.) This is "
               "a bug in the 'neo-compress' library.");
    }
}
