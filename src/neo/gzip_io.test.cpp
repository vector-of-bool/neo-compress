#include "./gzip_io.hpp"

#include <neo/string_io.hpp>

#include <catch2/catch.hpp>

TEST_CASE("Compress a simple string") {
    neo::string_dynbuf_io gz_data;
    neo::gzip_sink        gz_out{gz_data};

    neo::buffer_copy(gz_out, neo::const_buffer("Hello!"));
    gz_out.finish();

    neo::string_dynbuf_io plain;
    neo::gzip_source      gz_in{gz_data};
    neo::buffer_copy(plain, gz_in);
    CHECK(plain.string() == "Hello!");

    std::string pasta
        = "Did you ever hear the tragedy of Darth Plagueis The Wise? I thought not. It’s not a "
          "story the Jedi would tell you. It’s a Sith legend. Darth Plagueis was a Dark Lord of "
          "the Sith, so powerful and so wise he could use the Force to influence the midichlorians "
          "to create life… He had such a knowledge of the dark side that he could even keep the "
          "ones he cared about from dying. The dark side of the Force is a pathway to many "
          "abilities some consider to be unnatural. He became so powerful… the only thing he was "
          "afraid of was losing his power, which eventually, of course, he did. Unfortunately, he "
          "taught his apprentice everything he knew, then his apprentice killed him in his sleep. "
          "Ironic. He could save others from death, but not himself.";
    neo::string_dynbuf_io compressed;
    neo::gzip_compress(compressed, neo::const_buffer(pasta));
    neo::string_dynbuf_io decompressed;
    neo::gzip_decompress(decompressed, compressed);
    CHECK(decompressed.string() == pasta);
}
