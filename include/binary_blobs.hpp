#ifndef HEADER_BINARY_BLOBS_H
#define HEADER_BINARY_BLOBS_H

#include <stdint.h>

typedef struct {
    char *source;
    int length;
} BinaryBlob;

#if defined( REPGAME_ANDROID ) || defined( REPGAME_WASM )
#define MK_BLOB( prefix, name, postfix )
#else
#define MK_BLOB( prefix, name, postfix )                                                                                                                                                                                                       \
    extern char binary_##prefix##_##name##_##postfix##_start;                                                                                                                                                                                 \
    extern char binary_##prefix##_##name##_##postfix##_end;                                                                                                                                                                                \
    const BinaryBlob name = {.source = ( &binary_##prefix##_##name##_##postfix##_start ), .length = ( (int)(intptr_t)&binary_##prefix##_##name##_##postfix##_end - (int)(intptr_t)&binary_##prefix##_##name##_##postfix##_start )}

#endif
#endif