#include <neo/tar/ustar.hpp>

#include <neo/buffer_algorithm/decode.hpp>
#include <neo/buffer_algorithm/encode.hpp>
#include <neo/buffers_consumer.hpp>
#include <neo/dynbuf_io.hpp>
#include <neo/iostream_io.hpp>

#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace std::literals;

static const auto ROOT_DIR_PATH
    = std::filesystem::path(__FILE__).append("../../../..").lexically_normal();

TEST_CASE("Decode a single header") {
    std::ifstream    infile(ROOT_DIR_PATH / "data/test.tar", std::ios::binary);
    neo::iostream_io in{infile};

    neo::ustar_header_decoder dec;

    auto res = neo::buffer_decode(dec, in);
    CHECK(res.has_value());
    CHECK(res.bytes_read == 512);
}

TEST_CASE("Encode a single header") {
    neo::ustar_member_info meminfo;
    meminfo.set_filename("foo.txt");
    neo::ustar_header_encoder enc;
    std::string               buf;
    buf.resize(512);
    auto res = neo::buffer_encode(enc, neo::as_buffer(buf), meminfo);
    CHECK(res.done());
    // We'll have a lone filename at the beginning of the bytes
    CHECK(buf.substr(0, 7) == "foo.txt");
}

TEST_CASE("Read an archive") {
    neo::ustar_reader reader{
        neo::iostream_io{std::ifstream{ROOT_DIR_PATH / "data/test.tar", std::ios::binary}}};

    auto mem = reader.next_member().value();
    CHECK(mem.filename_str() == "01-test.txt");
    CHECK(mem.prefix_str() == "");
    CHECK(mem.size == 36);
    CHECK(mem.mode == 0b110'110'100);
    CHECK(mem.uid == 1000);
    CHECK(mem.is_regular_file());

    auto data = reader.all_data();
    CHECK(std::string_view(data) == "I am a file inside of a tar archive!"sv);

    mem = reader.next_member().value();
    CHECK(mem.is_regular_file());
    CHECK(mem.filename_str() == "02-test.txt");
    data = reader.all_data();
    CHECK(std::string_view(data) == "I am the second file!");

    mem = reader.next_member().value();
    CHECK(mem.filename_str() == "subdir/");
    CHECK(mem.is_directory());
    CHECK(mem.size == 0);

    mem = reader.next_member().value();
    CHECK(mem.filename_str() == "subdir/thing.txt");
    CHECK(mem.is_regular_file());
    CHECK(std::string_view(reader.all_data())
          == "I'm just another file, but in a subdirectory!\n\n- The Sign Painter");

    CHECK_FALSE(reader.next_member());
}

TEST_CASE("Write a ustar archive") {
    std::string out_str;

    neo::dynbuf_io io{out_str};

    neo::ustar_writer writer{io};

    neo::ustar_member_info mem;
    mem.set_filename("test.txt");
    mem.size = 5;
    writer.write_member(mem, neo::const_buffer("howdy"));
    writer.finish();

    neo::ustar_reader reader{io};
    mem = reader.next_member().value();
    CHECK(mem.filename_str() == "test.txt");
    CHECK(mem.size == 5);
    CHECK(std::string_view(reader.all_data()) == "howdy");
}

TEST_CASE("Add files to an archive from the filesystem") {
    std::string    out_str;
    neo::dynbuf_io io{out_str};

    neo::ustar_writer writer{io};
    writer.add_file("shakespeare.txt", ROOT_DIR_PATH / "data/shakespeare.txt");
    writer.add_file("shakespeare2.txt", ROOT_DIR_PATH / "data/shakespeare.txt");

    neo::ustar_reader reader{io};
    const auto        mem = reader.next_member().value();
    CHECK(mem.filename_str() == "shakespeare.txt");
    CHECK(mem.size >= 5'447'532);
    const auto content = std::string(reader.all_data());

    const auto mem2 = reader.next_member().value();
    CHECK(mem2.filename_str() == "shakespeare2.txt");
    CHECK(mem2.size == mem.size);
    CHECK(std::string_view(reader.all_data()) == content);
}
