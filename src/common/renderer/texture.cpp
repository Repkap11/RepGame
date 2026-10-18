#include <algorithm>
#include <cstring>
#include <vector>

#define GL_GLEXT_PROTOTYPES
#include "common/utils/file_utils.hpp"

#include "common/RepGame.hpp"
#include "common/renderer/texture.hpp"

#define BYTES_PER_PIXEL 4

// BMP files start with a 14-byte file header followed by a variable-size DIB
// header. Bytes 10-13 hold the little-endian offset of the pixel array, which
// lets the build embed the whole .bmp without stripping a fixed-size header.
static int bmp_pixel_data_offset( const unsigned char *bmp, int bmp_len ) {
    if ( bmp_len < 30 || bmp[ 0 ] != 'B' || bmp[ 1 ] != 'M' ) {
        pr_debug( "Texture is not a BMP file" );
        exit( 1 );
    }
    int bits_per_pixel = bmp[ 28 ] | ( bmp[ 29 ] << 8 );
    if ( bits_per_pixel != BYTES_PER_PIXEL * 8 ) {
        pr_debug( "Texture BMP must be %d bits per pixel, got:%d", BYTES_PER_PIXEL * 8, bits_per_pixel );
        exit( 1 );
    }
    return ( int )( ( unsigned )bmp[ 10 ] | ( ( unsigned )bmp[ 11 ] << 8 ) | ( ( unsigned )bmp[ 12 ] << 16 ) | ( ( unsigned )bmp[ 13 ] << 24 ) );
}

#if defined( REPGAME_ANDROID )
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
unsigned char *cached_texture[ 2 ];
void Texture::set_texture_data( unsigned int which_texture, unsigned char *textures, int textures_len ) {
    cached_texture[ which_texture ] = textures + bmp_pixel_data_offset( textures, textures_len );
}
#else
void Texture::set_texture_data( unsigned int which_texture, unsigned char *textures, int textures_len ) {
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
    unsigned char header[ 32 ];
    int header_len = ( int )fread( header, 1, sizeof( header ), file );
    fseek( file, bmp_pixel_data_offset( header, header_len ), SEEK_SET );
    data = ( unsigned char * )malloc( mem_size );
    size_t read_size = fread( data, mem_size, 1, file );
    if ( read_size != 1 ) {
        pr_debug( "Texture file wrong size. Expected:%d", mem_size );
    }
    fclose( file );
    return data;
}
#endif
#if defined( REPGAME_ANDROID )
unsigned char *readTextureDataFromCache( const char *filename, int mem_size ) {
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

// Cutout tiles (leaves, flowers, grass) store transparent texels as rgb=0, so
// glGenerateMipmap's box filter would bake black into every level — which is
// why the fragment shader used to un-premultiply — and would smooth alpha
// into a blurry field that collapses under the fragment shader's alpha test
// (the visible mip ring on foliage). Mips are generated on the CPU instead:
// holes inherit rgb from coloured neighbours, each level stores the
// alpha-weighted (unpremultiplied) rgb average, and binary cutout tiles keep
// alpha strictly {0,255} via nested subsampling — each level's hole pattern
// is a decimation of mip 0's own layout, so crossing a mip boundary coarsens
// the same pattern instead of swapping to a different speckle realization.

// Fill rgb of fully transparent texels from the nearest coloured ones so
// filtered samples near hole edges blend toward the right hue.
static void fill_transparent_rgb( unsigned char *tile, int tw, int th, int stride ) {
    std::vector<unsigned char> has_rgb( tw * th );
    int missing = 0;
    for ( int y = 0; y < th; y++ ) {
        for ( int x = 0; x < tw; x++ ) {
            unsigned char has = tile[ ( y * stride + x ) * BYTES_PER_PIXEL + 3 ] != 0;
            has_rgb[ y * tw + x ] = has;
            missing += !has;
        }
    }
    for ( int pass = 0; missing > 0 && pass < tw + th; pass++ ) {
        int filled = 0;
        for ( int y = 0; y < th; y++ ) {
            for ( int x = 0; x < tw; x++ ) {
                if ( has_rgb[ y * tw + x ] )
                    continue;
                int r = 0, g = 0, b = 0, n = 0;
                for ( int dy = -1; dy <= 1; dy++ ) {
                    for ( int dx = -1; dx <= 1; dx++ ) {
                        int nx = x + dx, ny = y + dy;
                        if ( nx < 0 || ny < 0 || nx >= tw || ny >= th || !has_rgb[ ny * tw + nx ] )
                            continue;
                        const unsigned char *p = tile + ( ny * stride + nx ) * BYTES_PER_PIXEL;
                        r += p[ 0 ];
                        g += p[ 1 ];
                        b += p[ 2 ];
                        n++;
                    }
                }
                if ( n ) {
                    unsigned char *p = tile + ( y * stride + x ) * BYTES_PER_PIXEL;
                    p[ 0 ] = r / n;
                    p[ 1 ] = g / n;
                    p[ 2 ] = b / n;
                    has_rgb[ y * tw + x ] = 1;
                    filled++;
                }
            }
        }
        missing -= filled;
    }
}

// 2x2 box downsample. rgb is the alpha-weighted (unpremultiplied) average so
// black holes don't darken the colour; a texel with no opaque ancestry
// inherits the plain rgb average of its already-filled parents.
static void downsample_tile( const unsigned char *src, int src_stride, int sw, int sh, unsigned char *dst, int dw, int dh ) {
    for ( int y = 0; y < dh; y++ ) {
        for ( int x = 0; x < dw; x++ ) {
            int wr = 0, wg = 0, wb = 0, ar = 0, ag = 0, ab = 0, aa = 0, n = 0;
            for ( int dy = 0; dy < 2; dy++ ) {
                int sy = 2 * y + dy < sh ? 2 * y + dy : sh - 1;
                for ( int dx = 0; dx < 2; dx++ ) {
                    int sx = 2 * x + dx < sw ? 2 * x + dx : sw - 1;
                    const unsigned char *p = src + ( sy * src_stride + sx ) * BYTES_PER_PIXEL;
                    int a = p[ 3 ];
                    wr += p[ 0 ] * a;
                    wg += p[ 1 ] * a;
                    wb += p[ 2 ] * a;
                    ar += p[ 0 ];
                    ag += p[ 1 ];
                    ab += p[ 2 ];
                    aa += a;
                    n++;
                }
            }
            unsigned char *o = dst + ( y * dw + x ) * BYTES_PER_PIXEL;
            if ( aa ) {
                o[ 0 ] = wr / aa;
                o[ 1 ] = wg / aa;
                o[ 2 ] = wb / aa;
            } else {
                o[ 0 ] = ar / n;
                o[ 1 ] = ag / n;
                o[ 2 ] = ab / n;
            }
            o[ 3 ] = aa / n;
        }
    }
}

// Binary cutout tiles: overwrite each mip texel's rgb with one of its source
// texels' rgb, picked by a stable per-position hash (alpha is left as the
// downsampled average the caller already wrote). Averaging rgb would wash the
// leaf texture's two-tone dither to flat mid-green — the visual transition at
// the mip boundary — so mip rgb keeps the authored palette at every level.
static void subsample_tile_rgb( const unsigned char *src, int src_stride, int sw, int sh, unsigned char *dst, int dw, int dh ) {
    int fx = sw / dw;
    int fy = sh / dh;
    for ( int dy = 0; dy < dh; dy++ ) {
        for ( int dx = 0; dx < dw; dx++ ) {
            unsigned int h = ( unsigned int )( dx ) * 1973u + ( unsigned int )( dy ) * 9277u;
            h ^= h >> 8;
            h *= 2654435761u;
            h ^= h >> 13;
            int cx = dx * fx + ( int )( h & ( unsigned int )( fx - 1 ) );
            int cy = dy * fy + ( int )( ( h >> 4 ) & ( unsigned int )( fy - 1 ) );
            memcpy( dst + ( dy * dw + dx ) * BYTES_PER_PIXEL, src + ( cy * src_stride + cx ) * BYTES_PER_PIXEL, BYTES_PER_PIXEL - 1 );
        }
    }
}

void Texture::loadTexture( const TextureSourceData &texture_source, int blur_mag ) {
    unsigned char *data;

    int mem_size = texture_source.width * texture_source.height * BYTES_PER_PIXEL;

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
#define TEXTURE_NEEDS_FREE 0
    int pixel_offset = bmp_pixel_data_offset( ( const unsigned char * )texture_source.blob->source, texture_source.blob->length );
    if ( texture_source.blob->length - pixel_offset != mem_size ) {
        int diff = texture_source.blob->length - pixel_offset - mem_size;
        pr_debug( "Unexpected Source Size blob:%d pixel_offset:%d calculated:%d diff:%d", texture_source.blob->length, pixel_offset, mem_size, diff );
        exit( 1 );
    }
    data = ( unsigned char * )texture_source.blob->source + pixel_offset;
#elif defined( REPGAME_WASM )
#define TEXTURE_NEEDS_FREE 1
    data = readTextureDataFromFile( texture_source.filename, mem_size );
#elif defined( REPGAME_ANDROID )
#define TEXTURE_NEEDS_FREE 0
    data = readTextureDataFromCache( texture_source.filename, mem_size );
#else
    data = NULL;
    pr_debug( "Undefined platform in textures.cpp mem_size:%d", mem_size );
#endif
    unsigned int textures_across = texture_source.width / texture_source.tile_size_across;
    unsigned int textures_down = texture_source.height / texture_source.tile_size_down;

    // Due to strange issues with block meshing and rotation, it's easier to flip images in the X direction here,
    // instead of in texture coordinates.
    int working_size = texture_source.width * texture_source.height * BYTES_PER_PIXEL * sizeof( char );
    unsigned char *working = ( unsigned char * )malloc( working_size );

    for ( unsigned int y = 0; y < textures_down; y++ ) {
        for ( unsigned int x = 0; x < textures_across; x++ ) {
            unsigned int corner_pixel = y * texture_source.width * texture_source.tile_size_down + x * texture_source.tile_size_across;
            for ( unsigned int pix_y = 0; pix_y < texture_source.tile_size_down; pix_y++ ) {
                for ( unsigned int pix_x = 0; pix_x < texture_source.tile_size_across; pix_x++ ) {
                    for ( unsigned int which_byte = 0; which_byte < BYTES_PER_PIXEL; which_byte++ ) {
                        working[ ( corner_pixel + pix_y * texture_source.width + ( texture_source.tile_size_across - pix_x - 1 ) ) * BYTES_PER_PIXEL + which_byte ] = //
                            data[ ( corner_pixel + pix_y * texture_source.width + pix_x ) * BYTES_PER_PIXEL + which_byte ];
                    }
                }
            }
        }
    }
    // BMP pixels are BGRA in memory; swap to the RGBA order GL expects.
    for ( int i = 0; i < working_size; i += 4 ) {
        unsigned char b = working[ i + 0 ];
        unsigned char r = working[ i + 2 ];
        working[ i + 0 ] = r;
        working[ i + 2 ] = b;
    }

    // Mipmaps eliminate distant texture aliasing/shimmer on high-frequency
    // textures (dirt, grass, leaves) on all platforms — and they also reduce
    // texture bandwidth on mobile. Anisotropic filtering (when supported)
    // sharpens oblique views without blurring head-on surfaces.
    // REPGAME_TEX_MIPMAPS=0 restores the old NEAREST-only sampling for A/B.
    static const bool tex_mipmaps = !( getenv( "REPGAME_TEX_MIPMAPS" ) && atoi( getenv( "REPGAME_TEX_MIPMAPS" ) ) == 0 );

    // Mip chain dimensions, halving to 1x1 like GL's own mipmapping would.
    int level_w[ 32 ], level_h[ 32 ];
    int mip_levels = 0;
    for ( int lw = texture_source.tile_size_across, lh = texture_source.tile_size_down;; ) {
        level_w[ mip_levels ] = lw;
        level_h[ mip_levels ] = lh;
        mip_levels++;
        if ( lw == 1 && lh == 1 )
            break;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }

    if ( tex_mipmaps ) {
        for ( unsigned int ty = 0; ty < textures_down; ty++ ) {
            for ( unsigned int tx = 0; tx < textures_across; tx++ ) {
                fill_transparent_rgb( working + ( ty * texture_source.width * texture_source.tile_size_down + tx * texture_source.tile_size_across ) * BYTES_PER_PIXEL,
                                      texture_source.tile_size_across, texture_source.tile_size_down, texture_source.width );
            }
        }
    }

    unsigned int layer_count = textures_across * textures_down;
    glGenTextures( 1, &this->m_RendererId );
    glBindTexture( this->target, this->m_RendererId );
    glTexImage3D( this->target,                                                   //
                  0,                                                              // mipLevelCount
                  this->internalFormat,                                           //
                  texture_source.tile_size_across, texture_source.tile_size_down, //
                  layer_count, 0, this->format, this->type, NULL );
    showErrors( );
    if ( tex_mipmaps ) {
        for ( int l = 1; l < mip_levels; l++ ) {
            glTexImage3D( this->target, l, this->internalFormat, level_w[ l ], level_h[ l ], layer_count, 0, this->format, this->type, NULL );
        }
        showErrors( );
    }

    glPixelStorei( GL_UNPACK_ROW_LENGTH, texture_source.width );
    glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, texture_source.height );
    for ( unsigned int i = 0; i < layer_count; i++ ) {
        int tex_coord_x = ( i % textures_across );
        int tex_coord_y = ( textures_down - 1 ) - ( i / textures_across );
        int text_coord_base = tex_coord_x * texture_source.tile_size_across + tex_coord_y * texture_source.tile_size_down * texture_source.width;
        glTexSubImage3D( this->target,                                                      //
                         0,                                                                 // Mipmap Level
                         0, 0, i,                                                           // offset
                         texture_source.tile_size_across, texture_source.tile_size_down, 1, //
                         this->format,
                         this->type, //
                         working + text_coord_base * BYTES_PER_PIXEL );
        showErrors( );
    }
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );

    if ( tex_mipmaps ) {
        std::vector<unsigned char> prev, cur;
        const int tw = texture_source.tile_size_across;
        const int th = texture_source.tile_size_down;
        for ( unsigned int i = 0; i < layer_count; i++ ) {
            int tex_coord_x = ( i % textures_across );
            int tex_coord_y = ( textures_down - 1 ) - ( i / textures_across );
            const unsigned char *src = working + ( tex_coord_x * tw + tex_coord_y * th * texture_source.width ) * BYTES_PER_PIXEL;
            int src_stride = texture_source.width;
            int sw = tw, sh = th;

            // Binary cutout tiles (alpha only ever 0 or 255 — leaves, flowers)
            // get the plain downsampled average alpha — the shader's hashed
            // alpha test turns sampled alpha into survival probability, so the
            // average IS the coverage-preserving choice at every level (even
            // the 1x1 mip keeps ~61% of its pixels instead of snapping to a
            // solid texel). Their rgb is still subsampled from one authored
            // texel so the two-tone dither palette survives instead of
            // washing to flat mid-green. Partial alpha means the tile isn't a
            // pure cutout (glass tints, AA edges) — it filters untouched.
            bool has_hole = false, has_partial = false;
            for ( int p = 0; p < sw * sh; p++ ) {
                unsigned char a = src[ ( p / sw * src_stride + p % sw ) * BYTES_PER_PIXEL + 3 ];
                has_hole |= a == 0;
                has_partial |= a != 0 && a != 255;
            }
            const bool cutout = has_hole && !has_partial;

            for ( int l = 1; l < mip_levels; l++ ) {
                int dw = level_w[ l ], dh = level_h[ l ];
                cur.resize( dw * dh * BYTES_PER_PIXEL );
                downsample_tile( src, src_stride, sw, sh, cur.data( ), dw, dh );
                if ( cutout )
                    subsample_tile_rgb( src, src_stride, sw, sh, cur.data( ), dw, dh );
                glTexSubImage3D( this->target, l, 0, 0, i, dw, dh, 1, this->format, this->type, cur.data( ) );
                // Chain off the level just uploaded.
                prev.swap( cur );
                src = prev.data( );
                src_stride = sw = dw;
                sh = dh;
            }
        }
        showErrors( );
    }
    free( working );

    glTexParameteri( this->target, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameteri( this->target, GL_TEXTURE_WRAP_T, GL_REPEAT );
    glTexParameteri( this->target, GL_TEXTURE_MIN_FILTER, tex_mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST );
    if ( blur_mag ) {
        glTexParameteri( this->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    } else {
        glTexParameteri( this->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    }

    // pr_debug( "Max ani:%f", max_ani );
    if ( TEXTURE_NEEDS_FREE ) {
        free( data );
    }
    if ( RepGame::supportsAnisotropic( ) ) {
        float max_ani;
        glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_ani );
        glTexParameterf( this->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_ani );
    }
}

static int next_slot = 1;

// Reserve a texture unit without an associated Texture — used for the shared
// per-chunk 3D light volume unit (light.cpp binds a different texture to it
// before every chunk draw). Shares the next_slot counter so it can't collide
// with regular textures.
int texture_reserve_unit( ) {
    return next_slot++;
}

void Texture::init( const TextureSourceData &texture_source, int blur_mag ) {
    this->slot = next_slot;
    next_slot++;
    this->target = GL_TEXTURE_2D_ARRAY;
    this->internalFormat = GL_RGBA8;
    this->format = GL_RGBA;
    this->type = GL_UNSIGNED_BYTE;
    glActiveTexture( GL_TEXTURE0 + this->slot );
    this->loadTexture( texture_source, blur_mag );
}

void Texture::init_empty_base( int blur_mag, int target, int internalFormat, int format, int type ) {
    this->slot = next_slot;
    next_slot++;
    this->target = target;
    this->internalFormat = internalFormat;
    this->format = format;
    this->type = type;
    glActiveTexture( GL_TEXTURE0 + this->slot );
    glGenTextures( 1, &this->m_RendererId );
    glBindTexture( this->target, this->m_RendererId );
    showErrors( );
    // pr_debug("Texture target:0x%x", this->target);
    if ( this->target == GL_TEXTURE_2D || this->target == GL_TEXTURE_2D_ARRAY ) {
        glTexParameteri( this->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        showErrors( );
        if ( blur_mag ) {
            glTexParameteri( this->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        } else {
            glTexParameteri( this->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        }
        glTexParameteri( this->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( this->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        if ( RepGame::supportsAnisotropic( ) ) {
            float max_ani;
            glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_ani );
            glTexParameterf( this->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_ani );
        }
    }
    showErrors( );

    // glBindTexture( this->target, 0 );
    showErrors( );
}

#if defined( REPGAME_ANDROID )
int SAMPLE_TARGET = GL_TEXTURE_2D;
#else
int SAMPLE_TARGET = GL_TEXTURE_2D_MULTISAMPLE;
#endif

void Texture::init_empty_color( int blur_mag ) {
    Texture::init_empty_base( blur_mag, SAMPLE_TARGET, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE );
}

void Texture::init_empty_depth_stencil( int blur_mag ) {
    Texture::init_empty_base( blur_mag, SAMPLE_TARGET, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8 );
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

void Texture::init_empty_depth( int blur_mag ) {
    Texture::init_empty_base( blur_mag, SAMPLE_TARGET, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT );
}

void Texture::change_size( int width, int height ) {
    glBindTexture( this->target, this->m_RendererId );
    // pr_debug( "Change size!!" );
    if ( this->target == GL_TEXTURE_2D_MULTISAMPLE ) {
#if defined( REPGAME_ANDROID ) || defined( REPGAME_WASM )
        pr_debug( "Error multi sample not supported on Android or WASM" );
        exit( 1 );
#else
        GLint maxSamples;
        glGetIntegerv( GL_MAX_SAMPLES, &maxSamples );
        maxSamples = ( maxSamples < MSAA_SAMPLES ) ? maxSamples : MSAA_SAMPLES;
        // pr_debug( "Max samples:%d", maxSamples );
        glTexImage2DMultisample( this->target, maxSamples, this->internalFormat, width, height, true );
#endif
    } else {
        glTexImage2D( this->target, 0, this->internalFormat, width, height, 0, this->format, this->type, NULL );
    }
}

void Texture::bind( ) const {
    glActiveTexture( GL_TEXTURE0 + this->slot );
    glBindTexture( this->target, this->m_RendererId );
}
void Texture::unbind( ) const {
    glActiveTexture( GL_TEXTURE0 + this->slot );
    glBindTexture( this->target, 0 );
}

void Texture::destroy( ) {
    glDeleteTextures( 1, &this->m_RendererId );
}