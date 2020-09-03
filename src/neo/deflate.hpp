#pragma once

#include <neo/compress.hpp>

#include "./detail/zlib_base.hpp"

#include <neo/buffer_algorithm/transform.hpp>

#include <array>
#include <cstddef>
#include <memory_resource>

namespace neo {

class deflate_compressor : public detail::compression_base {
public:
    explicit deflate_compressor(allocator_type alloc) noexcept;
    deflate_compressor() noexcept
        : deflate_compressor(allocator_type()) {}
    ~deflate_compressor();

    deflate_compressor(deflate_compressor&& o)
        : compression_base(NEO_FWD(o)) {}

    compress_result operator()(mutable_buffer out, const_buffer in, flush f = flush::no_flush);

    void reset() noexcept;
};

template <>
constexpr std::size_t buffer_transform_dynamic_growth_hint_v<deflate_compressor> = 1024 * 1024 * 4;

}  // namespace neo
