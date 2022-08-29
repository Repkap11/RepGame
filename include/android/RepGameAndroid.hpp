#ifndef HEADER_REPGAMEANDROID_H
#define HEADER_REPGAMEANDROID_H

#include <GLES3/gl31.h>
#include <malloc.h>
#include <stdlib.h>
#include <android/log.h>

char *repgame_android_getShaderString( const char *filename );

#define SUPPORTS_FRAME_BUFFER 0
#define REPGAME_PATH_DIVIDOR "/"
#define pr_debug( fmt, ... ) __android_log_print( ANDROID_LOG_INFO, "RepGameAndroid", "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );
#define pr_test( fmt, ... ) __android_log_print( ANDROID_LOG_INFO, "RepGameAndroid", fmt "\n", ##__VA_ARGS__ );

#define showErrors( )                                                                                                                                                                                                                          \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            pr_debug( "GL Error:0x%x", errCode );                                                                                                                                                                                              \
        }                                                                                                                                                                                                                                      \
    }

#endif