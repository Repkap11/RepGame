#ifndef HEADER_BINARY_BLOBS_H
#define HEADER_BLOCK_H

#include <stdint.h>

// extern char _binary_src_shaders_chunk_vertex_glsl_start;
// extern char _binary_src_shaders_chunk_vertex_glsl_size[];

// #define SHADER_CHUNK_VERTEX
// #define SHADER_CHUNK_VERTEX_SIZE

#define MK_SHADER( shader_name )                                                                                                                                                                                                               \
    extern char _binary_src_shaders_##shader_name##_glsl_start;                                                                                                                                                                                \
    extern char _binary_src_shaders_##shader_name##_glsl_size[];                                                                                                                                                                               \
    Shader_Source_Data shader_name = {.source = ( &_binary_src_shaders_##shader_name##_glsl_start ), .length = ( ( int )( intptr_t )_binary_src_shaders_##shader_name##_glsl_size )}

#endif