#pragma once


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

class Texture {
    friend class Shader;
    friend class Chunk;
    friend class FrameBuffer;
    friend class RepGame;

    unsigned int m_RendererId; //
    unsigned int slot;
    unsigned int target;
    int internalFormat;
    unsigned int format;
    unsigned int type;

    void loadTexture( const TextureSourceData &texture_source, int blur_mag );
    void init_empty_base( int blur_mag, int target, int internalFormat, int format, int type );

  public:
    void init( const TextureSourceData &texture_source, int blur_mag );
    void change_size( int width, int height );
    void init_empty_color( int blur_mag );
    void init_empty_depth_stencil( int blur_mag );
    static void set_texture_data( unsigned int which_texture, unsigned char *textures, int textures_len );

    void destroy( );
    void bind( ) const;
    void unbind( ) const;
};