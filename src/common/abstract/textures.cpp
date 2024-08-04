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
    unsigned char *data;

    int mem_size = texture_source->width * texture_source->height * BYTES_PER_PIXEL;

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
    unsigned char *working = ( unsigned char * )malloc( working_size );

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
                         working + text_coord_base * BYTES_PER_PIXEL );
        showErrors( );
    }
    free( working );

    glTexParameteri( texture->target, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameteri( texture->target, GL_TEXTURE_WRAP_T, GL_REPEAT );

    glTexParameteri( texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    if ( blur_mag ) {
        glTexParameteri( texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    } else {
        glTexParameteri( texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    }
    if ( repgame_supportsAnisotropic( ) ) {
    }

    // pr_debug( "Max ani:%f", max_ani );
    if ( TEXTURE_NEEDS_FREE ) {
        free( data );
    }
    if ( repgame_supportsAnisotropic( ) ) {
        float max_ani;
        glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_ani );
        glTexParameterf( texture->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_ani ); // TYhis doesn't work on Linux Mesa driver... it blurs textures
    }
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

void texture_init_empty_base( Texture *texture, int blur_mag, int target, int internalFormat, int format, int type ) {
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
    // pr_debug("Texture target:0x%x", texture->target);
    if ( texture->target == GL_TEXTURE_2D || texture->target == GL_TEXTURE_2D_ARRAY ) {
        glTexParameteri( texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        showErrors( );
        if ( blur_mag ) {
            glTexParameteri( texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        } else {
            glTexParameteri( texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        }
        glTexParameteri( texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        if ( repgame_supportsAnisotropic( ) ) {
            float max_ani;
            glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_ani );
            glTexParameterf( texture->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_ani );
        }
    }
    showErrors( );

    // glBindTexture( texture->target, 0 );
    showErrors( );
}

#if defined( REPGAME_ANDROID )
int SAMPLE_TARGET = GL_TEXTURE_2D;
#else
int SAMPLE_TARGET = GL_TEXTURE_2D_MULTISAMPLE;
#endif

void texture_init_empty_color( Texture *texture, int blur_mag ) {
    texture_init_empty_base( texture, blur_mag, SAMPLE_TARGET, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE );
}

void texture_init_empty_depth_stencil( Texture *depthTexture, int blur_mag ) {
    texture_init_empty_base( depthTexture, blur_mag, SAMPLE_TARGET, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8 );
    showErrors( );
#if defined( REPGAME_ANDROID ) || defined( REPGAME_WASM )
    pr_debug( "Error multi sample not supported on Android" );
    exit( 1 );
#else
    // glTexParameteri( SAMPLE_TARGET, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_DEPTH_COMPONENT );
    glTexParameteri( SAMPLE_TARGET, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_STENCIL_INDEX );
#endif
    showErrors( );
}

void texture_change_size( Texture *texture, int width, int height ) {
    glBindTexture( texture->target, texture->m_RendererId );
    // pr_debug( "Change size!!" );
    if ( texture->target == GL_TEXTURE_2D_MULTISAMPLE ) {
#if defined( REPGAME_ANDROID ) || defined( REPGAME_WASM )
        pr_debug( "Error multi sample not supported on Android or WASM" );
        exit( 1 );
#else
        GLint maxSamples;
        glGetIntegerv( GL_MAX_SAMPLES, &maxSamples );
        // pr_debug( "Max samples:%d", maxSamples );
        glTexImage2DMultisample( texture->target, maxSamples, texture->internalFormat, width, height, true );
#endif
    } else {
        glTexImage2D( texture->target, 0, texture->internalFormat, width, height, 0, texture->format, texture->type, NULL );
    }
}

void texture_bind( Texture *texture ) {
    glActiveTexture( GL_TEXTURE0 + texture->slot );
    glBindTexture( texture->target, texture->m_RendererId );
}
void texture_unbind( Texture *texture ) {
    glActiveTexture( GL_TEXTURE0 + texture->slot );
    glBindTexture( texture->target, 0 );
}

void texture_destroy( Texture *texture ) {
    glDeleteTextures( 1, &texture->m_RendererId );
}