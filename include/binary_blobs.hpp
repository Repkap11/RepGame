#pragma once

#include <stdint.h>

typedef struct {
    char *source;
    int length;
} BinaryBlob;

#if defined( REPGAME_WASM )
#define MK_BLOB( prefix, name, postfix )
#else
#define MK_BLOB( prefix, name, postfix )                                                                                                                                                                                                       \
    extern char _binary_##prefix##_##name##_##postfix##_start;                                                                                                                                                                                 \
    extern char _binary_##prefix##_##name##_##postfix##_end;                                                                                                                                                                                   \
    const BinaryBlob name = { .source = ( &_binary_##prefix##_##name##_##postfix##_start ),                                                                                                                                                    \
                              .length = ( int )( ( uintptr_t ) & _binary_##prefix##_##name##_##postfix##_end - ( uintptr_t ) & _binary_##prefix##_##name##_##postfix##_start ) }

#endif