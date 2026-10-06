#pragma once

#include <cstdio>

#if defined( REPGAME_ANDROID )
#include <android/log.h>
#endif

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );
#define pr_test( fmt, ... ) fprintf( stdout, fmt "\n", ##__VA_ARGS__ );
// Local replacement for gluErrorString so GLU isn't a dependency. Expands to
// an expression so the GL_* constants only need to exist where used.
#define REPGAME_GL_ERROR_STRING( errCode )                                              \
    ( ( errCode ) == GL_INVALID_ENUM                  ? "GL_INVALID_ENUM"               \
      : ( errCode ) == GL_INVALID_VALUE               ? "GL_INVALID_VALUE"              \
      : ( errCode ) == GL_INVALID_OPERATION           ? "GL_INVALID_OPERATION"          \
      : ( errCode ) == GL_STACK_OVERFLOW              ? "GL_STACK_OVERFLOW"             \
      : ( errCode ) == GL_STACK_UNDERFLOW             ? "GL_STACK_UNDERFLOW"            \
      : ( errCode ) == GL_OUT_OF_MEMORY               ? "GL_OUT_OF_MEMORY"              \
      : ( errCode ) == GL_INVALID_FRAMEBUFFER_OPERATION ? "GL_INVALID_FRAMEBUFFER_OPERATION" \
      : ( errCode ) == GL_CONTEXT_LOST                ? "GL_CONTEXT_LOST"               \
                                                      : "unknown" )
#ifdef REPGAME_SKIP_CHECK_FOR_GL_ERRORS
#define showErrors( )
#else
#define showErrors( )                                                                                                                                                                                                                          \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            pr_debug( "GL Error:%d:%s", errCode, REPGAME_GL_ERROR_STRING( errCode ) );                                                                                                                                                        \
            exit( 1 );                                                                                                                                                                                                                         \
        }                                                                                                                                                                                                                                      \
    }
#endif
#define ignoreErrors( )                                                                                                                                                                                                                        \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            pr_debug( "GL Ignoring Error:%d:%s", errCode, REPGAME_GL_ERROR_STRING( errCode ) );                                                                                                                                                 \
        }                                                                                                                                                                                                                                      \
    }
#endif

#if defined( REPGAME_ANDROID )
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

#if defined( REPGAME_WASM )
#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );
#define pr_test( fmt, ... ) fprintf( stdout, fmt "\n", ##__VA_ARGS__ );
#define showErrors( )                                                                                                                                                                                                                          \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            pr_debug( "GL Error:%d:%s", errCode, "Not Defines on WASM" );                                                                                                                                                                      \
        }                                                                                                                                                                                                                                      \
    }
#define ignoreErrors( )                                                                                                                                                                                                                        \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            pr_debug( "GL Ignoring Error:%d", errCode );                                                                                                                                                                                       \
        }                                                                                                                                                                                                                                      \
    }
#endif