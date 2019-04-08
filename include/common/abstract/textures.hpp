#ifndef HEADER_TEXTURES_H
#define HEADER_TEXTURES_H

#include "binary_blobs.hpp"
typedef struct {
#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
    const BinaryBlob *blob;
#else
    const char *filename;
#endif
    int width;
    int height;
    int tile_size_across;
    int tile_size_down;
    int header_size;
} TextureSourceData;

// header_size must match trim value in Makefile for linux and windows binaries
#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
#define MK_TEXTURE( name, width_, height_, tile_size_across_, tile_size_down_, header_size_ )                                                                                                                                                  \
    MK_BLOB( out_bitmaps, name, bin );                                                                                                                                                                                                         \
    const TextureSourceData texture_source_##name = {.blob = &name, .width = width_, .height = height_, .tile_size_across = tile_size_across_, .tile_size_down = tile_size_down_, .header_size = 0}
#else

#define MK_TEXTURE( name, width_, height_, tile_size_across_, tile_size_down_, header_size_ )                                                                                                                                                  \
    const TextureSourceData texture_source_##name = {                                                                                                                                                                                          \
        .filename = "bitmaps" REPGAME_PATH_DIVIDOR #name ".bin",                                                                                                                                                                               \
        .width = width_,                                                                                                                                                                                                                       \
        .height = height_,                                                                                                                                                                                                                     \
        .tile_size_across = tile_size_across_,                                                                                                                                                                                                 \
        .tile_size_down = tile_size_down_,                                                                                                                                                                                                     \
        .header_size = 0,                                                                                                                                                                                                           \
    }
#endif

typedef struct {
    unsigned int m_RendererId; //
    unsigned int slot;
} Texture;

#include "common/block_definitions.hpp"

void textures_set_texture_data( unsigned int which_texture, unsigned char *textures, int textures_len );

void texture_init( Texture *texture, const TextureSourceData *texture_source );
void texture_destroy( Texture *texture );
void texture_bind( Texture *texture, unsigned int texture_slot );
void texture_unbind( Texture *texture, unsigned int texture_slot );
#endif
