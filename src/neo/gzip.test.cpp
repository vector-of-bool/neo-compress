#include <neo/gzip.hpp>

#include <neo/deflate.hpp>
#include <neo/inflate.hpp>

#include <neo/buffer_algorithm/transform.hpp>
#include <neo/dynbuf_io.hpp>
#include <neo/iostream_io.hpp>

#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

static const auto ROOT_DIR_PATH
    = std::filesystem::path(__FILE__).append("../../..").lexically_normal();

TEST_CASE("Compress/decompress some data") {
    // Compress a bit of data.
    neo::gzip_compressor<neo::deflate_compressor> comp;

    std::string text
        = "Did you ever hear the tragedy of Darth Plagueis The Wise? I thought not. It’s not a "
          "story the Jedi would tell you. It’s a Sith legend. Darth Plagueis was a Dark Lord of "
          "the Sith, so powerful and so wise he could use the Force to influence the midichlorians "
          "to create life… He had such a knowledge of the dark side that he could even keep the "
          "ones he cared about from dying. The dark side of the Force is a pathway to many "
          "abilities some consider to be unnatural. He became so powerful… the only thing he was "
          "afraid of was losing his power, which eventually, of course, he did. Unfortunately, he "
          "taught his apprentice everything he knew, then his apprentice killed him in his sleep. "
          "Ironic. He could save others from death, but not himself.";
    neo::dynbuf_io<std::string> gzipped;
    auto res = neo::buffer_transform(comp, gzipped, neo::const_buffer(text), neo::flush::finish);
    gzipped.shrink_uncommitted();
    CHECK(res.bytes_read == text.size());
    CHECK(res.done);

    neo::gzip_decompressor<neo::inflate_decompressor> decomp;

    neo::dynbuf_io<std::string> io_orig;
    auto decomp_res = neo::buffer_transform(decomp, io_orig, neo::const_buffer(gzipped.storage()));
    io_orig.shrink_uncommitted();
    CHECK(io_orig.storage().size() == decomp_res.bytes_written);
    CHECK(io_orig.storage() == text);
}

TEST_CASE("Decompress with metadata") {
    std::ifstream    infile{ROOT_DIR_PATH / "data/asdf.txt.gz", std::ios::binary};
    neo::iostream_io gz_in{infile};

    neo::dynbuf_io<std::string> plain;
    buffer_transform(neo::gzip_decompressor<neo::inflate_decompressor>(), plain, gz_in);
    plain.shrink_uncommitted();
    CHECK(plain.storage() == "asdf");
}
