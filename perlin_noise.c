#include "perlin_noise.h"
#include <math.h>
#include <stdlib.h>

int perlin_noise( int x, int y, int z ) {
    if ( y == 0 ) {
        return 1;
    }
    if ( y < 0 ) {
        return 0;
    }
    if ( cos( sin( ( double )( abs( x + z ) % 20 ) / 10.0 ) ) > ( y ) + -abs( z ) / 10.0 ) {
        return 1;
    }
    return 0;
}