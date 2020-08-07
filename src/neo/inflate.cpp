#include "./inflate.hpp"

#include <neo/c/miniz/miniz.h>

#define MY_STATE reinterpret_cast<::tinfl_decompressor&>(_state_bytes)

/**
 * Initialize the state for tinfl.
 */
neo::inflate_decompressor::inflate_decompressor() noexcept {
    // Check that we've given it enough room to actually stores its data:
    static_assert(sizeof(_state_bytes) == sizeof(::tinfl_decompressor));
    // Initialize tinfl state
    auto st = new (&MY_STATE)::tinfl_decompressor{};
    tinfl_init(st);
}

void neo::inflate_decompressor::reset() noexcept {
    // Simply re-initialize the state
    tinfl_init(&MY_STATE);
}

neo::decompress_result neo::inflate_decompressor::operator()(neo::mutable_buffer out,
                                                             neo::const_buffer   in) {
    auto in_size  = in.size();
    auto out_size = out.size();

    auto result
        = ::tinfl_decompress(&MY_STATE,
                             reinterpret_cast<const mz_uint8*>(in.data()),
                             &in_size,
                             reinterpret_cast<mz_uint8*>(out.data()),
                             reinterpret_cast<mz_uint8*>(out.data()),
                             &out_size,
                             TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | TINFL_FLAG_HAS_MORE_INPUT);
    assert(result != TINFL_STATUS_BAD_PARAM);

    if (result < int(TINFL_STATUS_DONE)) {
        // There was an error from tinfl!
        throw std::runtime_error("Data inflate failed. Corrupted?");
    }

    return {
        .bytes_written = out_size,
        .bytes_read    = in_size,
        .done          = result == TINFL_STATUS_DONE,
    };
}
