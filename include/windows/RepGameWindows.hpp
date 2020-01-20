#ifndef HEADER_REPGAMEWINDOWS_H
#define HEADER_REPGAMEWINDOWS_H

#include <GL/glew.h>
#include "common/RepGameSDL2.hpp"

#ifndef M_PI
#define M_PI ( 3.14159265358979323846 )
#endif

#define REPGAME_PATH_DIVIDOR "\\"
#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );
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
        }                                                                                                                                                                                                                                      \
    }

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
#endif