#ifndef HEADER_REPGAMEWASM_H
#define HEADER_REPGAMEWASM_H

#include <GL/glew.h>
#include <GL/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <emscripten.h>

#define REPGAME_PATH_DIVIDOR "/"
//#define pr_debug( fmt, ... )
#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );
#define showErrors( )                                                                                                                                                                                                                          \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            pr_debug( "GL Error:%d:%s", errCode, "Not Defines on WASM" );                                                                                                                                                                      \
        }                                                                                                                                                                                                                                      \
    }

#endif