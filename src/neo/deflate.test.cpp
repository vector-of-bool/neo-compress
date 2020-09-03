#include <neo/deflate.hpp>

#include <neo/dynbuf_io.hpp>

#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

static const auto ROOT_DIR_PATH
    = std::filesystem::path(__FILE__).append("../../..").lexically_normal();

TEST_CASE("Compress some data") {
    neo::deflate_compressor c;

    std::string text = "Hello, DEFLATE!";
    std::string comp;
    comp.resize(64);
    auto res = neo::buffer_transform(c,
                                     neo::mutable_buffer(comp),
                                     neo::const_buffer(text),
                                     neo::flush::finish);
    CHECK(res.bytes_read == text.size());
    CHECK(res.done);
}

TEST_CASE("Compress with not enough output room") {
    neo::deflate_compressor c;

    std::string text = "Hello, DEFLATE!";
    std::string out;
    out.resize(5);
    auto res = neo::buffer_transform(c,
                                     neo::mutable_buffer(out),
                                     neo::const_buffer(text),
                                     neo::flush::finish);
    CHECK(res.bytes_read == text.size());
    CHECK_FALSE(res.done);
}

TEST_CASE("Compress into a dynamic buffer") {
    neo::dynbuf_io<std::string> compressed;
    std::string                 text
        = "Did you ever hear the tragedy of Darth Plagueis The Wise? I thought not. It’s not a "
          "story the Jedi would tell you. It’s a Sith legend. Darth Plagueis was a Dark Lord of "
          "the Sith, so powerful and so wise he could use the Force to influence the midichlorians "
          "to create life… He had such a knowledge of the dark side that he could even keep the "
          "ones he cared about from dying. The dark side of the Force is a pathway to many "
          "abilities some consider to be unnatural. He became so powerful… the only thing he was "
          "afraid of was losing his power, which eventually, of course, he did. Unfortunately, he "
          "taught his apprentice everything he knew, then his apprentice killed him in his sleep. "
          "Ironic. He could save others from death, but not himself.";
    neo::deflate_compressor defl;
    auto                    res = neo::buffer_transform(defl, compressed, neo::const_buffer(text));
    res += neo::buffer_transform(defl, compressed, neo::const_buffer(), neo::flush::finish);
    compressed.shrink_uncommitted();
    CHECK(res.done);
    CHECK(res.bytes_read == text.size());
    CHECK(compressed.storage().size() > 0);
    CHECK(res.bytes_written == compressed.storage().size());
}

TEST_CASE("Compress streaming") {
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

    neo::deflate_compressor c;
    std::string             text = "Hello, DEFLATE!";
    auto                    res  = neo::buffer_transform(c, mbuf_seq, neo::const_buffer(text));
    res += neo::buffer_transform(c, mbuf_seq, neo::const_buffer(), neo::flush::finish);
    CHECK(res.bytes_read == text.size());
    CHECK(res.bytes_written <= sizeof mbufs_arrs);
    CHECK(res.done);
}

TEST_CASE("Big compress") {
    std::ifstream shakespeare_infile{ROOT_DIR_PATH / "data/shakespeare.txt", std::ios::binary};
    REQUIRE(shakespeare_infile.is_open());

    std::stringstream strm;
    strm << shakespeare_infile.rdbuf();

    const std::string big_str = std::move(strm).str();

    neo::dynbuf_io<std::string> compressed;

    neo::deflate_compressor defl;
    auto defl_res = neo::buffer_transform(defl, compressed, neo::const_buffer(big_str));
    defl_res += neo::buffer_transform(defl, compressed, neo::const_buffer(), neo::flush::finish);

    compressed.shrink_uncommitted();
    CHECK(defl_res.bytes_read == big_str.size());
    CHECK(defl_res.bytes_written == compressed.storage().size());
}
