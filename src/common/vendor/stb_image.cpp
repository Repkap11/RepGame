#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
// stb trips -Werror on the Windows build (mingw g++): STBI_ONLY_PNG strands
// the overflow helpers used by the removed loaders, and gcc's inliner flags a
// bogus maybe-uninitialized in stbi__compute_transparency16.
#if defined( __GNUC__ ) && !defined( __clang__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "common/vendor/stb_image.hpp"
#if defined( __GNUC__ ) && !defined( __clang__ )
#pragma GCC diagnostic pop
#endif