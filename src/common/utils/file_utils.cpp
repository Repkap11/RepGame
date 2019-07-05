#include "common/utils/file_utils.hpp"
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined( REPGAME_LINUX )
char *getRepGamePath( ) {
    char buffer[ BUFSIZ ];
    memset( buffer, 0, sizeof( buffer ) );
    ssize_t result = readlink( "/proc/self/exe", buffer, BUFSIZ - 1 );
    if ( result == -1 ) {
        return NULL;
    }
    char *dir_temp = dirname( buffer );
    char *dir = strdup( dir_temp );
    return dir;
}
#endif
#if defined( REPGAME_ANDROID )
char *getRepGamePath( ) {
    char *dir_temp = ( char * )".";
    char *dir = strdup( dir_temp );
    return dir;
}
#endif
#if defined( REPGAME_WINDOWS )
char *getRepGamePath( ) {
    char *dir_temp = ( char * )".";
    char *dir = strdup( dir_temp );
    return dir;
}
#endif
#if defined( REPGAME_WASM )
char *getRepGamePath( ) {
    char *dir_temp = ( char * )"";
    char *dir = strdup( dir_temp );
    return dir;
}
#endif
