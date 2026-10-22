#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
// stb trips -Werror: STBI_ONLY_PNG strands the overflow helpers used by the
// removed loaders, and gcc's inliner flags a bogus maybe-uninitialized in
// stbi__compute_transparency16 (the group doesn't exist on clang).
#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#if !defined( __clang__ )
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#endif
#include "common/vendor/stb_image.hpp"
#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC diagnostic pop
#endif