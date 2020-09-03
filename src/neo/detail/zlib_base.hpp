#pragma once

#include <array>
#include <cstddef>
#include <memory_resource>

namespace neo::detail {

class compression_base {
public:
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

protected:
    struct _zstream_proto {
        std::byte*    next_in;
        unsigned      avail_in;
        unsigned long total_in;
        std::byte*    next_out;
        unsigned      avail_out;
        unsigned long total_out;
        const char*   msg;
        void*         state;
        void (*alloc_fn)();
        void (*free_fn)();
        void*         opaque;
        int           data_type;
        unsigned long adler;
        unsigned long reserved;
    };

    allocator_type _alloc;

    void* _z_stream_ptr = nullptr;

    explicit compression_base(allocator_type alloc);
    compression_base(compression_base&&) noexcept;
    ~compression_base();

public:
    allocator_type get_allocator() const noexcept { return _alloc; }
};

}  // namespace neo::detail