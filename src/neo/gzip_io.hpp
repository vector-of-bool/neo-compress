#pragma once

#include "./deflate.hpp"
#include "./gzip.hpp"
#include "./inflate.hpp"

#include <neo/buffer_sink.hpp>
#include <neo/buffer_source.hpp>
#include <neo/transform_io.hpp>

namespace neo {

/**
 * @brief Adapt a buffer_sink with gzip-based compression.
 *
 * @tparam Sink The underlying buffer sink (A file, socket, etc.)
 */
template <buffer_sink Sink>
class gzip_sink : public buffer_transform_sink<Sink, gzip_compressor<deflate_compressor>> {
public:
    explicit gzip_sink(Sink&& out)
        : gzip_sink::buffer_transform_sink{NEO_FWD(out), {}} {}

    std::size_t finish() {
        return buffer_transform(this->transformer(), this->sink(), const_buffer(), flush::finish)
            .bytes_written;
    }
};

template <buffer_sink S>
explicit gzip_sink(S &&) -> gzip_sink<S>;

/**
 * @brief Adapt a buffer_source with gzip-based decompression.
 *
 * @tparam Source The underlying buffer source (A file, socket, etc.)
 */
template <buffer_source Source>
class gzip_source
    : public buffer_transform_source<Source, gzip_decompressor<inflate_decompressor>> {
public:
    explicit gzip_source(Source&& in)
        : gzip_source::buffer_transform_source{NEO_FWD(in), {}} {}
};

template <buffer_source S>
explicit gzip_source(S &&) -> gzip_source<S>;

/**
 * @brief Compress the given input and write it as a gzip-stream to the given output.
 *
 * @returns the number of bytes written to the output.
 */
template <buffer_output Out, buffer_input In>
std::size_t gzip_compress(Out&& out, In&& in) {
    gzip_sink gz_out{ensure_buffer_sink(out)};
    auto      n = buffer_copy(gz_out, in);
    n += gz_out.finish();
    return n;
}

/**
 * @brief Decompress the given gzip-compressed input, and write the decompressed data to the given
 * output.
 *
 * @returns The number of bytes written to the output.
 */
template <buffer_output Out, buffer_input In>
std::size_t gzip_decompress(Out&& out, In&& in) {
    gzip_source gz_in{ensure_buffer_source(in)};
    auto        n = buffer_copy(out, gz_in);
    return n;
}

}  // namespace neo
