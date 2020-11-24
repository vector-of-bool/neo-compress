#include "./zlib_base.hpp"

#include <zlib.h>

using namespace neo;
using namespace neo::detail;

compression_base::compression_base(detail::compression_base::allocator_type alloc)
    : _alloc(alloc) {
    _z_stream_ptr = get_allocator().allocate(sizeof(::z_stream));

    auto z_st    = new (_z_stream_ptr)::z_stream{};
    z_st->zalloc = [](void* self, unsigned count, unsigned size) noexcept -> void* {
        auto        alloc   = reinterpret_cast<compression_base*>(self)->get_allocator();
        std::size_t n_bytes = size * count;
        return alloc.allocate(n_bytes);
    };
    z_st->zfree = [](void* self, void* addr) noexcept {
        auto alloc = reinterpret_cast<compression_base*>(self)->get_allocator();
        alloc.deallocate(static_cast<std::byte*>(addr), 0);
    };
    z_st->opaque = this;
}

compression_base::compression_base(compression_base&& other) noexcept
    : _alloc(other.get_allocator())
    , _z_stream_ptr(std::exchange(other._z_stream_ptr, nullptr)) {
    static_cast<::z_stream*>(_z_stream_ptr)->opaque = this;
}

compression_base::~compression_base() {
    if (_z_stream_ptr) {
        get_allocator().deallocate(static_cast<std::byte*>(_z_stream_ptr), 0);
    }
}
