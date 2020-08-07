#include <neo/gzip.hpp>

#include <neo/deflate.hpp>
#include <neo/inflate.hpp>

#include <neo/as_dynamic_buffer.hpp>
#include <neo/buffer_algorithm/transform.hpp>

#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

static const auto ROOT_DIR_PATH
    = std::filesystem::path(__FILE__).append("../../..").lexically_normal();

TEST_CASE("Compress/decompress some data") {
    // Compress a bit of data.
    neo::gzip_compressor<neo::deflate_compressor> comp;

    std::string dest;
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
    auto res = neo::buffer_transform(comp,
                                     neo::as_dynamic_buffer(dest),
                                     neo::const_buffer(text),
                                     neo::flush::finish);
    CHECK(res.bytes_read == text.size());
    CHECK(res.done);

    neo::gzip_decompressor<neo::inflate_decompressor> decomp;

    std::string orig;
    auto        decomp_res
        = neo::buffer_transform(decomp, neo::as_dynamic_buffer(orig), neo::const_buffer(dest));

    CHECK(orig == text);
}

TEST_CASE("Decompress with metadata") {
    std::ifstream     infile{ROOT_DIR_PATH / "data/asdf.txt.gz", std::ios::binary};
    std::stringstream strm;
    strm << infile.rdbuf();

    std::string gz_content = strm.str();
    std::string plaintext;

    buffer_transform(neo::gzip_decompressor<neo::inflate_decompressor>(),
                     neo::as_dynamic_buffer(plaintext),
                     neo::const_buffer(gz_content));
    CHECK(plaintext == "asdf");
}
