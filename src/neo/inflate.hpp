#pragma once

#include <neo/decompress.hpp>

#include <neo/buffer_algorithm/transform.hpp>

#include <array>
#include <cstddef>

namespace neo {

/**
 * A buffer transformer that takes decompresses a sequence of bytes that have
 * been compressed using the DEFLATE algorithm.
 */
class inflate_decompressor {
    // These prototypes for the tinfl state are yanked from the tinfl headers, more-or-less, with
    // some constants written directly.
    struct _tinfl_huff_table_proto {
        std::uint8_t m_code_size[288];
        std::int16_t m_look_up[1024], m_tree[288 * 2];
    };
    struct _tinfl_state_proto {
        std::uint32_t m_state, m_num_bits, m_zhdr0, m_zhdr1, m_z_adler32, m_final, m_type,
            m_check_adler32, m_dist, m_counter, m_num_extra, m_table_sizes[3];
        std::int64_t            m_bit_buf;
        std::size_t             m_dist_from_out_buf_start;
        _tinfl_huff_table_proto m_tables[3];
        std::uint8_t            m_raw_header[4], m_len_codes[288 + 32 + 137];
    };

    /**
     * Declare an array of bytes that will hold the tinfl state. This allows us
     * to store tinfl state within the object (no dynamic allocation) without
     * needing to pul in the full tinfl definition as a dependent header.
     */
    alignas(_tinfl_state_proto) std::array<std::byte, sizeof(_tinfl_state_proto)> _state_bytes;

public:
    inflate_decompressor() noexcept;
    decompress_result operator()(mutable_buffer out, const_buffer in);

    void reset() noexcept;
};

template <>
constexpr std::size_t buffer_transform_dynamic_growth_hint_v<inflate_decompressor> = 1024 * 8;

}  // namespace neo
