#ifndef HEADER_TEXTURES_H
#define HEADER_TEXTURES_H

#include "binary_blobs.hpp"
typedef struct {
#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
    const BinaryBlob *blob;
#else
    const char *filename;
#endif
    unsigned int width;
    unsigned int height;
    unsigned int tile_size_across;
    unsigned int tile_size_down;
} TextureSourceData;

// header_size must match trim value in Makefile for linux and windows binaries
#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
#define MK_TEXTURE( name, width_, height_, tile_size_across_, tile_size_down_ )                                                                                                                                                                \
    MK_BLOB( out_bitmaps, name, bin );                                                                                                                                                                                                         \
    const TextureSourceData texture_source_##name = { .blob = &name, .width = width_, .height = height_, .tile_size_across = tile_size_across_, .tile_size_down = tile_size_down_ }
#else

#define MK_TEXTURE( name, width_, height_, tile_size_across_, tile_size_down_ )                                                                                                                                                                \
    const TextureSourceData texture_source_##name = {                                                                                                                                                                                          \
        .filename = "bitmaps" REPGAME_PATH_DIVIDOR #name ".bin",                                                                                                                                                                               \
        .width = width_,                                                                                                                                                                                                                       \
        .height = height_,                                                                                                                                                                                                                     \
        .tile_size_across = tile_size_across_,                                                                                                                                                                                                 \
        .tile_size_down = tile_size_down_,                                                                                                                                                                                                     \
    }
#endif

typedef struct {
    unsigned int m_RendererId; //
    unsigned int slot;
    unsigned int target;
    int internalFormat;
    unsigned int format;
    unsigned int type;
} Texture;

void textures_set_texture_data( unsigned int which_texture, unsigned char *textures, int textures_len );

void texture_init( Texture *texture, const TextureSourceData *texture_source, int blur_mag );
void texture_change_size( Texture *texture, int width, int height );
void texture_init_empty_color( Texture *texture, int blur_mag );
void texture_init_empty_depth_stencil( Texture *depthTexture, int blur_mag );
void texture_destroy( Texture *texture );
void texture_bind( Texture *texture );
void texture_unbind( Texture *texture );
#endif
