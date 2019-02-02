#include "common/utils/file_utils.hpp"
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef REPGAME_LINUX
char *getRepGamePath( ) {
    char buffer[ BUFSIZ ];
    memset( buffer, 0, sizeof( buffer ) );
    ssize_t result = readlink( "/proc/self/exe", buffer, BUFSIZ - 1 );
    char *dir;
    if ( result == -1 ) {
        return NULL;
    }
    dir = dirname( buffer );
    return dir;
}
#endif
#ifdef REPGAME_WASM
char *getRepGamePath( ) {
    return (char*)"";
}
#endif
