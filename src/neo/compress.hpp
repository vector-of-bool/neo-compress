#pragma once

#include <neo/buffer_algorithm/transform.hpp>

namespace neo {

struct compress_result {
    std::size_t bytes_written = 0;
    std::size_t bytes_read    = 0;
    bool        done          = false;

    compress_result& operator+=(const compress_result& o) noexcept {
        bytes_written += o.bytes_written;
        bytes_read += o.bytes_read;
        done = done || o.done;
        return *this;
    }
};

enum class flush {
    no_flush = 0,
    partial  = 1,
    sync     = 2,
    full     = 3,
    finish   = 4,
    block    = 5,
    // trees    = 6,  // Not available in miniz
};

constexpr flush operator&(flush l, flush r) noexcept { return flush(int(l) & int(r)); }
constexpr flush operator|(flush l, flush r) noexcept { return flush(int(l) | int(r)); }
constexpr flush operator^(flush l, flush r) noexcept { return flush(int(l) ^ int(r)); }

// clang-format off
template <typename T>
concept compressor_algorithm =
    buffer_transformer<T> &&
    same_as<buffer_transform_result_t<T>, compress_result> &&
    requires(T tr) {
        { tr.reset() } noexcept;
    };
// clang-format on

template <compressor_algorithm Algo, dynamic_buffer Out, buffer_range Input>
compress_result compress(Out&& output, Input&& in) {
    thread_local Algo compressor;
    compressor.reset();
    return compressor.compress_more_finish(output, in);
}

}  // namespace neo
