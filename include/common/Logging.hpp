#ifndef HEADER_LOGGING_H
#define HEADER_LOGGING_H

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );
#define pr_test( fmt, ... ) fprintf( stdout, fmt "\n", ##__VA_ARGS__ );
#endif

#if defined( REPGAME_ANDROID )
#define pr_debug( fmt, ... ) __android_log_print( ANDROID_LOG_INFO, "RepGameAndroid", "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );
#define pr_test( fmt, ... ) __android_log_print( ANDROID_LOG_INFO, "RepGameAndroid", fmt "\n", ##__VA_ARGS__ );
#endif

#if defined( REPGAME_WASM )
#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );
#define pr_test( fmt, ... ) fprintf( stdout, fmt "\n", ##__VA_ARGS__ );
#endif

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