#include <cstring>

#define GL_GLEXT_PROTOTYPES
#include "common/utils/file_utils.hpp"

#include "common/RepGame.hpp"
#include "common/abstract/textures.hpp"

#if defined( REPGAME_ANDROID )
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
unsigned char *cached_texture[ 2 ];
void textures_set_texture_data( unsigned int which_texture, unsigned char *textures, int textures_len ) {
    for ( int i = 0; i < textures_len; i += 4 ) {
        unsigned char a = textures[ i + 0 ];
        unsigned char b = textures[ i + 1 ];
        unsigned char g = textures[ i + 2 ];
        unsigned char r = textures[ i + 3 ];
        textures[ i + 0 ] = r;
        textures[ i + 1 ] = g;
        textures[ i + 2 ] = b;
        textures[ i + 3 ] = a;
    }
    cached_texture[ which_texture ] = textures;
}
#else
void textures_set_texture_data( unsigned int which_texture, unsigned char *textures, int textures_len ) {
    pr_debug( "textures_set_texture_data doesnt need to be called on Linux WASM or Windows" );
}
#endif

#if defined( REPGAME_WASM )
unsigned char *readTextureDataFromFile( const char *filename, int mem_size ) {
    unsigned char *data;
    FILE *file;
    pr_debug( "Reading texture:%s", filename );
    file = fopen( filename, "rb" );
    if ( file == NULL ) {
        pr_debug( "Error rendering texture %s", filename );
        return 0;
    }
    data = ( unsigned char * )malloc( mem_size );
    size_t read_size = fread( data, mem_size, 1, file );
    if ( read_size != 1 ) {
        pr_debug( "Texture file wrong size. Expected:%d", mem_size );
    }
    fclose( file );
    for ( int i = 0; i < mem_size; i += 4 ) {
        unsigned char a = data[ i + 0 ];
        unsigned char b = data[ i + 1 ];
        unsigned char g = data[ i + 2 ];
        unsigned char r = data[ i + 3 ];
        data[ i + 0 ] = r;
        data[ i + 1 ] = g;
        data[ i + 2 ] = b;
        data[ i + 3 ] = a;
    }
    return data;
}
#endif
#if defined( REPGAME_ANDROID )
unsigned char *readTextureDataFromCache( const char *filename, int mem_size ) {
    // TODO this is such a hack
    pr_debug( "Returning texture for %s", filename );
    if ( strcmp( filename, "bitmaps/textures.bin" ) == 0 )
        return cached_texture[ 0 ];
    if ( strcmp( filename, "bitmaps/sky4.bin" ) == 0 )
        return cached_texture[ 1 ];
    pr_debug( "Cant find a texture for:%s", filename );
    exit( 1 );
    return NULL;
}
#endif

#define BYTES_PER_PIXEL 4
void loadTexture( Texture *texture, const TextureSourceData *texture_source, int blur_mag ) {
    int bmp_header = texture_source->header_size;
    unsigned char *data;

    int mem_size = texture_source->width * texture_source->height * BYTES_PER_PIXEL + bmp_header;

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
#define TEXTURE_NEEDS_FREE 0
    if ( texture_source->blob->length != mem_size ) {
        int diff = texture_source->blob->length - mem_size;
        pr_debug( "Unexpected Source Size blob:%d calculated:%d diff:%d", texture_source->blob->length, mem_size, diff );
        exit( 1 );
    }
    data = ( unsigned char * )texture_source->blob->source;
#elif defined( REPGAME_WASM )
#define TEXTURE_NEEDS_FREE 1
    data = readTextureDataFromFile( texture_source->filename, mem_size );
#elif defined( REPGAME_ANDROID )
#define TEXTURE_NEEDS_FREE 0
    data = readTextureDataFromCache( texture_source->filename, mem_size );
#else
    data = NULL;
    pr_debug( "Undefined platform in textures.cpp mem_size:%d", mem_size );
#endif
    unsigned int textures_across = texture_source->width / texture_source->tile_size_across;
    unsigned int textures_down = texture_source->height / texture_source->tile_size_down;

    // Due to strange issues with block meshing and rotation, it's easier to flip images in the X direction here,
    // instead of in texture coordinates.
    int working_size = texture_source->width * texture_source->height * BYTES_PER_PIXEL * sizeof( char );
    unsigned char *working = ( unsigned char * )malloc( working_size + bmp_header );

    for ( unsigned int y = 0; y < textures_down; y++ ) {
        for ( unsigned int x = 0; x < textures_across; x++ ) {
            unsigned int corner_pixel = y * texture_source->width * texture_source->tile_size_down + x * texture_source->tile_size_across;
            for ( unsigned int pix_y = 0; pix_y < texture_source->tile_size_down; pix_y++ ) {
                for ( unsigned int pix_x = 0; pix_x < texture_source->tile_size_across; pix_x++ ) {
                    for ( unsigned int which_byte = 0; which_byte < BYTES_PER_PIXEL; which_byte++ ) {
                        working[ ( corner_pixel + pix_y * texture_source->width + ( texture_source->tile_size_across - pix_x - 1 ) ) * BYTES_PER_PIXEL + which_byte ] = //
                            data[ ( corner_pixel + pix_y * texture_source->width + pix_x ) * BYTES_PER_PIXEL + which_byte ];
                    }
                }
            }
        }
    }
    for ( int i = 0; i < working_size; i += 4 ) {
        unsigned char a = working[ i + 0 ];
        unsigned char b = working[ i + 1 ];
        unsigned char g = working[ i + 2 ];
        unsigned char r = working[ i + 3 ];
        working[ i + 0 ] = b;
        working[ i + 1 ] = g;
        working[ i + 2 ] = r;
        working[ i + 3 ] = a;
    }

    unsigned int layer_count = textures_across * textures_down;
    glGenTextures( 1, &texture->m_RendererId );
    glBindTexture( texture->target, texture->m_RendererId );
    glTexImage3D( texture->target,                                                  //
                  0,                                                                // mipLevelCount
                  texture->internalFormat,                                          //
                  texture_source->tile_size_across, texture_source->tile_size_down, //
                  layer_count, 0, texture->format, texture->type, NULL );
    showErrors( );

    glPixelStorei( GL_UNPACK_ROW_LENGTH, texture_source->width );
    glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, texture_source->height );
    for ( unsigned int i = 0; i < layer_count; i++ ) {
        int tex_coord_x = ( i % textures_across );
        int tex_coord_y = ( textures_down - 1 ) - ( i / textures_across );
        int text_coord_base = tex_coord_x * texture_source->tile_size_across + tex_coord_y * texture_source->tile_size_down * texture_source->width;
        glTexSubImage3D( texture->target,                                                     //
                         0,                                                                   // Mipmap Level
                         0, 0, i,                                                             // offset
                         texture_source->tile_size_across, texture_source->tile_size_down, 1, //
                         texture->format,
                         texture->type, //
                         working + bmp_header + text_coord_base * BYTES_PER_PIXEL );
        showErrors( );
    }
    free( working );

    glTexParameterf( texture->target, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameterf( texture->target, GL_TEXTURE_WRAP_T, GL_REPEAT );
    glTexParameterf( texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    if ( blur_mag ) {
        glTexParameterf( texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    } else {
        glTexParameterf( texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    }
    float max_ani;
    glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_ani );
    // pr_debug( "Max ani:%f", max_ani );
    if ( TEXTURE_NEEDS_FREE ) {
        free( data );
    }
    glTexParameterf( texture->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_ani );
    // glGenerateMipmap( texture->target );
}

static int next_slot = 1;
void texture_init( Texture *texture, const TextureSourceData *texture_source, int blur_mag ) {
    texture->slot = next_slot;
    next_slot++;
    texture->target = GL_TEXTURE_2D_ARRAY;
    texture->internalFormat = GL_RGBA8;
    texture->format = GL_RGBA;
    texture->type = GL_UNSIGNED_BYTE;
    glActiveTexture( GL_TEXTURE0 + texture->slot );
    loadTexture( texture, texture_source, blur_mag );
}

void texture_init_empty_base( Texture *texture, int width, int height, int blur_mag, int target, int internalFormat, int format, int type ) {
    texture->slot = next_slot;
    next_slot++;
    texture->target = target;
    texture->internalFormat = internalFormat;
    texture->format = format;
    texture->type = type;
    glActiveTexture( GL_TEXTURE0 + texture->slot );
    glGenTextures( 1, &texture->m_RendererId );
    glBindTexture( texture->target, texture->m_RendererId );
    showErrors( );
    if ( texture->target == GL_TEXTURE_2D_MULTISAMPLE ) {
        glTexImage2DMultisample( texture->target, MULTI_SAMPLE_SCALE, internalFormat, width, height, false );
    } else {
        glTexImage2D( texture->target, 0, internalFormat, width, height, 0, format, type, NULL );
        glTexParameteri( texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        showErrors( );
        if ( blur_mag ) {
            glTexParameterf( texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        } else {
            glTexParameterf( texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        }
        glTexParameterf( texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameterf( texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        float max_ani;
        glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_ani );
        glTexParameterf( texture->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_ani );
    }
    showErrors( );

    // glBindTexture( texture->target, 0 );
    showErrors( );
}

void texture_init_empty_color( Texture *texture, int width, int height, int blur_mag ) {
    // texture_init_empty_base( texture, width, height, blur_mag, GL_TEXTURE_2D, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE );
    texture_init_empty_base( texture, width, height, blur_mag, GL_TEXTURE_2D_MULTISAMPLE, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE );
}

void texture_init_empty_depth_stencil( Texture *texture, int width, int height, int blur_mag ) {
    texture_init_empty_base( texture, width, height, blur_mag, GL_TEXTURE_2D_MULTISAMPLE, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8 );
}

void texture_change_size( Texture *texture, int width, int height ) {
    glBindTexture( texture->target, texture->m_RendererId );
    if ( texture->target == GL_TEXTURE_2D_MULTISAMPLE ) {
        glTexImage2DMultisample( texture->target, MULTI_SAMPLE_SCALE, texture->internalFormat, width, height, false );
    } else {
        glTexImage2D( texture->target, 0, texture->internalFormat, width, height, 0, texture->format, texture->type, NULL );
    }
}

void texture_bind( Texture *texture, unsigned int texture_slot ) {
    glActiveTexture( GL_TEXTURE0 + texture_slot );
    glBindTexture( texture->target, texture->m_RendererId );
}
void texture_unbind( Texture *texture, unsigned int texture_slot ) {
    glActiveTexture( GL_TEXTURE0 + texture_slot );
    glBindTexture( texture->target, 0 );
}

void texture_destroy( Texture *texture ) {
    glDeleteTextures( 1, &texture->m_RendererId );
}