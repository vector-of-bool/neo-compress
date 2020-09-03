#pragma once

#include <neo/decompress.hpp>

#include "./detail/zlib_base.hpp"

#include <neo/buffer_algorithm/transform.hpp>

#include <array>
#include <cstddef>
#include <memory_resource>

namespace neo {

/**
 * A buffer transformer that takes decompresses a sequence of bytes that have
 * been compressed using the DEFLATE algorithm.
 */
class inflate_decompressor : public detail::compression_base {
public:
    explicit inflate_decompressor(allocator_type alloc) noexcept;
    inflate_decompressor() noexcept
        : inflate_decompressor(allocator_type()) {}
    ~inflate_decompressor();

    inflate_decompressor(inflate_decompressor&& o)
        : compression_base(NEO_FWD(o)) {}

    decompress_result operator()(mutable_buffer out, const_buffer in);

    void reset() noexcept;
};

template <>
constexpr std::size_t buffer_transform_dynamic_growth_hint_v<inflate_decompressor> = 1024 * 1024
    * 4;

}  // namespace neo
