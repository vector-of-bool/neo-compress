#include <neo/compress.hpp>

#include <neo/as_dynamic_buffer.hpp>
#include <neo/buffer_algorithm/copy.hpp>

#include <catch2/catch.hpp>

namespace {

// A dumb compressor that just copies bytes from the input into the output
struct dummy_algo {
    neo::compress_result operator()(neo::mutable_buffer out,
                                    neo::const_buffer   in,
                                    neo::flush          f = neo::flush::no_flush) noexcept {
        auto n_copied = neo::buffer_copy(out, in);
        return {
            .bytes_written = n_copied,
            .bytes_read    = n_copied,
            .done          = f == neo::flush::finish && n_copied == in.size(),
        };
    }

    void reset() noexcept {}
};

}  // namespace

TEST_CASE("Compress some data") {
    dummy_algo c;

    std::string text = "Hello, DEFLATE!";
    std::string comp;
    comp.resize(50);
    auto res = neo::buffer_transform(c, neo::mutable_buffer(comp), neo::const_buffer(text));
    CHECK(res.bytes_read == text.size());
}

TEST_CASE("Compress with not enough output room") {
    dummy_algo c;

    std::string text = "Hello, DEFLATE!";
    std::string out;
    out.resize(5);
    auto res = neo::buffer_transform(c,
                                     neo::mutable_buffer(out),
                                     neo::const_buffer(text),
                                     neo::flush::finish);
    CHECK(res.bytes_read == out.size());
}

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

TEST_CASE("Compress into a dynamic buffer") {
    std::string buf;
    dummy_algo  c;
    auto        res = neo::buffer_transform(c,
                                     neo::as_dynamic_buffer(buf),
                                     neo::const_buffer(pasta),
                                     neo::flush::finish);
    CHECK(res.done);
    CHECK(res.bytes_read == pasta.size());
    CHECK(buf.size() > 0);
    CHECK(res.bytes_written == buf.size());
    CHECK(buf == pasta);
}

TEST_CASE("Compress multi-part input") {
    auto bufs = {
        neo::const_buffer(
            "Did you ever hear the tragedy of Darth Plagueis The Wise? I thought not. It’s not a "),
        neo::const_buffer("story the Jedi would tell you. It’s a Sith legend. Darth Plagueis was a "
                          "Dark Lord of "),
        neo::const_buffer("the Sith, so powerful and so wise he could use the Force to influence "
                          "the midichlorians "),
        neo::const_buffer("to create life… He had such a knowledge of the dark side that he could "
                          "even keep the "),
        neo::const_buffer(
            "ones he cared about from dying. The dark side of the Force is a pathway to many "),
        neo::const_buffer("abilities some consider to be unnatural. He became so powerful… the "
                          "only thing he was "),
        neo::const_buffer("afraid of was losing his power, which eventually, of course, he did. "
                          "Unfortunately, he "),
        neo::const_buffer("taught his apprentice everything he knew, then his apprentice killed "
                          "him in his sleep. "),
        neo::const_buffer("Ironic. He could save others from death, but not himself."),
    };
    std::string buf;
    dummy_algo  algo;
    auto        res = neo::buffer_transform(algo, neo::as_dynamic_buffer(buf), bufs);
    res += neo::buffer_transform(algo,
                                 neo::as_dynamic_buffer(buf),
                                 neo::const_buffer(),
                                 neo::flush::finish);
    CHECK(res.done);
    CHECK(buf == pasta);
    CHECK(buf.size() == neo::buffer_size(bufs));
}
