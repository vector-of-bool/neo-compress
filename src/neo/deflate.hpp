#pragma once

#include <neo/compress.hpp>

#include <neo/buffer_algorithm/transform.hpp>

#include <array>
#include <cstddef>

namespace neo {

class deflate_compressor {
    struct _tdefl_state_proto {
        void*          m_pPut_buf_func;
        void*          m_pPut_buf_user;
        unsigned       m_flags, m_max_probes[2];
        int            m_greedy_parsing;
        unsigned       m_adler32, m_lookahead_pos, m_lookahead_size, m_dict_size;
        unsigned char *m_pLZ_code_buf, *m_pLZ_flags, *m_pOutput_buf, *m_pOutput_buf_end;
        unsigned       m_num_flags_left, m_total_lz_bytes, m_lz_code_buf_dict_pos, m_bits_in,
            m_bit_buffer;
        unsigned m_saved_match_dist, m_saved_match_len, m_saved_lit, m_output_flush_ofs,
            m_output_flush_remaining, m_finished, m_block_index, m_wants_to_finish;
        int                  m_prev_return_status;
        const void*          m_pIn_buf;
        void*                m_pOut_buf;
        std::size_t *        m_pIn_buf_size, *m_pOut_buf_size;
        int                  m_flush;
        const unsigned char* m_pSrc;
        std::size_t          m_src_buf_left, m_out_buf_ofs;
        unsigned char        m_dict[32768 + 258 - 1];
        std::uint16_t        m_huff_count[3][288];
        std::uint16_t        m_huff_codes[3][288];
        unsigned char        m_huff_code_sizes[3][288];
        unsigned char        m_lz_code_buf[65536];
        std::uint16_t        m_next[32768];
        std::uint16_t        m_hash[32768];
        unsigned char        m_output_buf[85196];
    };

    std::array<std::byte, sizeof(_tdefl_state_proto)> _state_bytes;

public:
    deflate_compressor() noexcept;
    compress_result operator()(mutable_buffer out, const_buffer in, flush f = flush::no_flush);

    void reset() noexcept;
};

template <>
constexpr std::size_t buffer_transform_dynamic_growth_hint_v<deflate_compressor> = 1024 * 8;

}  // namespace neo
