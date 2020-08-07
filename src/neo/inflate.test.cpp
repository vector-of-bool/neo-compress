#include <neo/deflate.hpp>
#include <neo/inflate.hpp>

#include <neo/as_dynamic_buffer.hpp>

#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

static const auto ROOT_DIR_PATH
    = std::filesystem::path(__FILE__).append("../../..").lexically_normal();

TEST_CASE("Compress some data") {
    std::string defl_str;
    defl_str.resize(50);

    {
        neo::deflate_compressor c;
        std::string             text = "Hello, DEFLATE!";
        auto                    res  = neo::buffer_transform(c,
                                         neo::mutable_buffer(defl_str),
                                         neo::const_buffer(text),
                                         neo::flush::finish);
        CHECK(res.bytes_read == text.size());
        defl_str.resize(res.bytes_written);
        CHECK(res.done);
    }
    {
        neo::inflate_decompressor decomp;
        std::string               res_str;
        res_str.resize(50);
        // Get the DEFLATE data that we generated
        auto res = neo::buffer_transform(decomp,
                                         neo::mutable_buffer(res_str),
                                         neo::const_buffer(defl_str));
        res_str.resize(res.bytes_written);
        CHECK(res_str == "Hello, DEFLATE!");
    }
}

TEST_CASE("Decompress streaming") {
    std::array<std::byte, 10> mbufs_arrs[] = {
        std::array<std::byte, 10>(),
        std::array<std::byte, 10>(),
        std::array<std::byte, 10>(),
        std::array<std::byte, 10>(),
        std::array<std::byte, 10>(),
    };

    auto mbuf_seq = {
        neo::mutable_buffer(mbufs_arrs[0]),
        neo::mutable_buffer(mbufs_arrs[1]),
        neo::mutable_buffer(mbufs_arrs[2]),
        neo::mutable_buffer(mbufs_arrs[3]),
        neo::mutable_buffer(mbufs_arrs[4]),
    };

    {
        neo::deflate_compressor c;
        std::string             text = "Hello, DEFLATE!";
        auto res = neo::buffer_transform(c, mbuf_seq, neo::const_buffer(text), neo::flush::finish);
        CHECK(res.bytes_read == text.size());
        CHECK(res.bytes_written <= sizeof mbufs_arrs);
        CHECK(res.done);
    }
    {
        neo::inflate_decompressor decomp;
        std::string               res_str;
        res_str.resize(50);
        // Get the DEFLATE data that we generated
        auto res = neo::buffer_transform(decomp, neo::mutable_buffer(res_str), mbuf_seq);
        res_str.resize(res.bytes_written);
        CHECK(res_str == "Hello, DEFLATE!");
    }
}

TEST_CASE("Big compress/decompress") {
    std::ifstream shakespeare_infile{ROOT_DIR_PATH / "data/shakespeare.txt", std::ios::binary};
    REQUIRE(shakespeare_infile.is_open());

    std::stringstream strm;
    strm << shakespeare_infile.rdbuf();

    const std::string big_str = std::move(strm).str();

    std::string big_compressed;

    auto defl_res = neo::buffer_transform(neo::deflate_compressor(),
                                          neo::as_dynamic_buffer(big_compressed),
                                          neo::const_buffer(big_str),
                                          neo::flush::finish);
    CHECK(defl_res.bytes_read == big_str.size());
    CHECK(defl_res.bytes_written == big_compressed.size());

    std::string big_decompressed;

    auto infl_res = neo::buffer_transform(neo::inflate_decompressor(),
                                          neo::as_dynamic_buffer(big_decompressed),
                                          neo::const_buffer(big_compressed));
    CHECK(infl_res.bytes_written == big_str.size());
    CHECK(big_str == big_decompressed);
}
