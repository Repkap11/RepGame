#include "common/utils/file_utils.hpp"
#include "common/RepGame.hpp"
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined( REPGAME_LINUX )
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
char *getRepGamePath( ) {
    char buffer[ BUFSIZ ];
    memset( buffer, 0, sizeof( buffer ) );
    ssize_t result = readlink( "/proc/self/exe", buffer, BUFSIZ - 1 );
    if ( result == -1 ) {
        return NULL;
    }
    // char *dir_temp = dirname( buffer );
    // char *dir = strdup( dir_temp );
    char *dir;

    if ( ( dir = getenv( "HOME" ) ) == NULL ) {
        dir = getpwuid( getuid( ) )->pw_dir;
    }
    // strcpy(dir, "/home/paul/rep_image");
    char *fullPath = (char*)malloc(BUFSIZ * sizeof(char));
    snprintf( fullPath, BUFSIZ, "%s%s.repgame", dir, REPGAME_PATH_DIVIDOR);
    //free(dir);
    return fullPath;
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
