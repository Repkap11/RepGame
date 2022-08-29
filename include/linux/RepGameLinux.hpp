#ifndef HEADER_REPGAMELINUX_H
#define HEADER_REPGAMELINUX_H

#include <GL/glew.h>
#include "common/RepGameSDL2.hpp"

#define SUPPORTS_FRAME_BUFFER 1
#define REPGAME_PATH_DIVIDOR "/"
#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d :%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );
#define pr_test( fmt, ... ) fprintf( stdout, fmt "\n", ##__VA_ARGS__ );
#ifdef REPGAME_FAST
#define showErrors( )
#else
#define showErrors( )                                                                                                                                                                                                                          \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        const GLubyte *errString;                                                                                                                                                                                                              \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            errString = gluErrorString( errCode );                                                                                                                                                                                             \
            pr_debug( "GL Error:%d:%s", errCode, errString );                                                                                                                                                                                  \
            exit( 1 );                                                                                                                                                                                                                         \
        }                                                                                                                                                                                                                                      \
    }
#endif

#define ignoreErrors( )                                                                                                                                                                                                                        \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        const GLubyte *errString;                                                                                                                                                                                                              \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            errString = gluErrorString( errCode );                                                                                                                                                                                             \
            pr_debug( "GL Ignoring Error:%d:%s", errCode, errString );                                                                                                                                                                         \
        }                                                                                                                                                                                                                                      \
    }

#endif