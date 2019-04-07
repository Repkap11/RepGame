#ifndef HEADER_BINARY_BLOBS_H
#define HEADER_BINARY_BLOBS_H

#include <stdint.h>

typedef struct {
    char *source;
    int length;
} BinaryBlob;

#if defined( REPGAME_ANDROID ) || defined( REPGAME_WASM )
#define MK_BLOB( shader_name )
#else
#define MK_BLOB( prefix, name )                                                                                                                                                                                                               \
    extern char _binary_##prefix##_##name##_glsl_start;                                                                                                                                                                                \
    extern char _binary_##prefix##_##name##_glsl_size[];                                                                                                                                                                               \
    const BinaryBlob name = {.source = ( &_binary_##prefix##_##name##_glsl_start ), .length = ( ( int )( intptr_t )_binary_##prefix##_##name##_glsl_size )}

#endif
#endif