#include "common/RepGame.hpp"
#include "common/renderer/screenshot.hpp"

#if ( SUPPORTS_FRAME_BUFFER )
#include <fstream>
#include <vector>
#include "common/utils/file_utils.hpp"

static void saveTGA( const std::string &filename, int width, int height, const unsigned char *pixels ) {
    // Simple uncompressed BGRA TGA writer.
    std::ofstream file( filename, std::ios::binary );
    if ( !file.is_open( ) ) {
        pr_debug( "Screenshot: could not open %s", filename.c_str( ) );
        return;
    }
    unsigned char header[ 18 ] = { 0 };
    header[ 2 ] = 2; // uncompressed RGB
    header[ 12 ] = width & 0xff;
    header[ 13 ] = ( width >> 8 ) & 0xff;
    header[ 14 ] = height & 0xff;
    header[ 15 ] = ( height >> 8 ) & 0xff;
    header[ 16 ] = 32; // bits per pixel
    header[ 17 ] = 0x00; // bottom-left origin (glReadPixels is bottom-left)
    file.write( reinterpret_cast<const char *>( header ), 18 );
    // glReadPixels returns RGBA; TGA expects BGRA, so swap R and B.
    int pixelCount = width * height;
    std::vector<unsigned char> bgra( pixelCount * 4 );
    for ( int i = 0; i < pixelCount; i++ ) {
        bgra[ i * 4 + 0 ] = pixels[ i * 4 + 2 ]; // B
        bgra[ i * 4 + 1 ] = pixels[ i * 4 + 1 ]; // G
        bgra[ i * 4 + 2 ] = pixels[ i * 4 + 0 ]; // R
        bgra[ i * 4 + 3 ] = pixels[ i * 4 + 3 ]; // A
    }
    file.write( reinterpret_cast<const char *>( bgra.data( ) ), pixelCount * 4 );
    file.close( );
    pr_debug( "Screenshot saved: %s (%dx%d)", filename.c_str( ), width, height );
}

void take_screenshot( unsigned int frameBufferId, const std::string &prefix ) {
    // Ensure the screenshots directory exists.
    mkdir_p( "screenshots" );

    GLint viewport[ 4 ];
    glGetIntegerv( GL_VIEWPORT, viewport );
    int width = viewport[ 2 ];
    int height = viewport[ 3 ];
    int pixelCount = width * height;
    std::vector<unsigned char> pixels( pixelCount * 4 );

    // Create a temporary resolve framebuffer (non-multisample) so we can
    // blit the multisample attachments into it and read them with glReadPixels.
    GLuint resolveFBO;
    glGenFramebuffers( 1, &resolveFBO );
    glBindFramebuffer( GL_FRAMEBUFFER, resolveFBO );
    GLuint resolveTex;
    glGenTextures( 1, &resolveTex );
    glBindTexture( GL_TEXTURE_2D, resolveTex );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0 );

    const char *names[] = { "blockTexture", "reflectionTexture", "fogTexture", "skyColorTexture" };
    for ( int i = 0; i < 4; i++ ) {
        // Blit (resolve) multisample attachment i into the resolve FBO.
        glBindFramebuffer( GL_READ_FRAMEBUFFER, frameBufferId );
        glReadBuffer( GL_COLOR_ATTACHMENT0 + i );
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, resolveFBO );
        glDrawBuffer( GL_COLOR_ATTACHMENT0 );
        glBlitFramebuffer( 0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
        // Read from the resolve FBO.
        glBindFramebuffer( GL_READ_FRAMEBUFFER, resolveFBO );
        glReadBuffer( GL_COLOR_ATTACHMENT0 );
        glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data( ) );
        // The fog texture (index 2) stores the fog factor in alpha only (RGB=0).
        // Copy alpha into RGB so it's visible as grayscale in image viewers.
        if ( i == 2 ) {
            for ( int p = 0; p < pixelCount; p++ ) {
                unsigned char a = pixels[ p * 4 + 3 ];
                pixels[ p * 4 + 0 ] = a;
                pixels[ p * 4 + 1 ] = a;
                pixels[ p * 4 + 2 ] = a;
            }
        }
        saveTGA( "screenshots/" + prefix + "_" + names[ i ] + ".tga", width, height, pixels.data( ) );
    }

    // Cleanup resolve FBO.
    glDeleteTextures( 1, &resolveTex );
    glDeleteFramebuffers( 1, &resolveFBO );

    // Also capture the display framebuffer (final composited result).
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    glReadBuffer( GL_FRONT );
    glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data( ) );
    saveTGA( "screenshots/" + prefix + "_display.tga", width, height, pixels.data( ) );

    pr_debug( "Screenshot complete: %s_*", prefix.c_str( ) );
}
#else
void take_screenshot( unsigned int frameBufferId, const std::string &prefix ) {
    // Screenshots not supported on this platform (no framebuffer support).
}
#endif
