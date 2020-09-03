#pragma once

#include <neo/buffer_algorithm/transform.hpp>
#include <neo/buffers_consumer.hpp>
#include <neo/concepts.hpp>
#include <neo/const_buffer.hpp>
#include <neo/dynamic_buffer.hpp>
#include <neo/mutable_buffer.hpp>

namespace neo {

struct decompress_result {
    std::size_t bytes_written = 0;
    std::size_t bytes_read    = 0;
    bool        done          = false;

    decompress_result& operator+=(const decompress_result& o) noexcept {
        bytes_written += o.bytes_written;
        bytes_read += o.bytes_read;
        done = done || o.done;
        return *this;
    }
};

static_assert(buffer_transform_result<decompress_result>);

// clang-format off
template <typename T>
concept decompressor_algorithm =
    buffer_transformer<T> &&
    same_as<buffer_transform_result_t<T>, decompress_result> &&
    requires(T tr) {
        { tr.reset() } noexcept;
    };
// clang-format on

template <decompressor_algorithm Algorithm>
class basic_decomperssor {
public:
    using algorithm_type = Algorithm;

private:
    algorithm_type _impl;

public:
    void reset() noexcept { _impl.reset(); }

    template <mutable_buffer_range Output, buffer_range Input>
    decompress_result decompress_more(Output&& out_, Input&& in_) noexcept {
        buffers_consumer out{out_};
        buffers_consumer in{in_};

        decompress_result acc;
        while (1) {
            auto [nwritten, nread, done]
                = _impl.decompress(out.next_contiguous(), in.next_contiguous());
            out.consume(nwritten);
            in.consume(nread);
            acc.bytes_written += nwritten;
            acc.bytes_read += nread;
            acc.done = done;
            if (nread == 0 || in.empty()) {
                break;
            }
        }
        return acc;
    }

    template <dynamic_buffer Output, buffer_range Input>
    auto decompress_more(Output&& out, Input&& in) noexcept {
        return _compress_more_append(out, in);
    }
};

}  // namespace neo
