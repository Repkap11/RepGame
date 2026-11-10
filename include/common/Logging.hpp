#pragma once

#include <cstdio>
#include <cstdlib>

#if defined( __GLIBC__ )
#include <execinfo.h>
#include <unistd.h>
#endif

#if defined( REPGAME_ANDROID )
#include <android/log.h>
#endif

// Fatal exit for "cannot continue" paths (showErrors, FBO/shader/texture
// failures, ...). Must not be exit(): exit runs static destructors, and
// ChunkLoader's file-static light_threads are typically still joinable, so
// ~thread() -> std::terminate -> abort anyway — after tearing down libraries
// (libamdhip64) under live workers. abort() lets the kernel reap the threads.
// fflush keeps the pr_debug line that reported the error (stdout is fully
// buffered when piped); the glibc backtrace preserves the fault site in the
// log even without a core.
[[noreturn]] inline void repgame_fatal( ) {
    fflush( NULL );
#if defined( __GLIBC__ )
    void *frames[ 64 ];
    const int n = backtrace( frames, 64 );
    if ( n > 0 ) {
        backtrace_symbols_fd( frames, n, STDERR_FILENO );
    }
#endif
    abort( );
}

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
            repgame_fatal( );                                                                                                                                                                                                                         \
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
// glGetError is a synchronous GPU-process round-trip in Firefox's remoted
// WebGL, so per-frame showErrors() calls serialize the whole pipeline. Like
// the Linux/Windows release builds, REPGAME_SKIP_CHECK_FOR_GL_ERRORS compiles
// them out.
#ifdef REPGAME_SKIP_CHECK_FOR_GL_ERRORS
#define showErrors( )
#else
#define showErrors( )                                                                                                                                                                                                                          \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            pr_debug( "GL Error:%d:%s", errCode, "Not Defines on WASM" );                                                                                                                                                                      \
        }                                                                                                                                                                                                                                      \
    }
#endif
#define ignoreErrors( )                                                                                                                                                                                                                        \
    {                                                                                                                                                                                                                                          \
        int errCode;                                                                                                                                                                                                                           \
        if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {                                                                                                                                                                                    \
            pr_debug( "GL Ignoring Error:%d", errCode );                                                                                                                                                                                       \
        }                                                                                                                                                                                                                                      \
    }
#endif