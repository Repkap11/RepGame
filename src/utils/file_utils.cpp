#include "utils/file_utils.h"
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

char *getRepGamePath( ) {
    char buffer[ BUFSIZ ];
    memset( buffer, 0, sizeof( buffer ) );
    readlink( "/proc/self/exe", buffer, BUFSIZ - 1 );
    char *dir;
    dir = dirname( buffer );
    return dir;
}
