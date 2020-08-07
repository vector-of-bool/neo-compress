#pragma once

// This file is part of neo-compress, and sets various miniz-related configuration options

// We don't use stdio:
#define MINIZ_NO_STDIO

// We always provide our own allocators:
#define MINIZ_NO_MALLOC

// Don't #define a bunch of zlib names. We'll just use the miniz ones
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
