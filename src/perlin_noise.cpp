#include "perlin_noise.h"
#include "RepGame.h"
#include <math.h>
#include <stdlib.h>
#include "constants.h"

#include <stdio.h>

static int hash[] = {208, 34,  231, 213, 32,  248, 233, 56,  161, 78,  24,  140, 71,  48,  140, 254, 245, 255, 247, 247, 40,  185, 248, 251, 245, 28,  124, 204, 204, 76,  36,  1,   107, 28,  234, 163, 202, 224, 245, 128, 167, 204, 9,
                     92,  217, 54,  239, 174, 173, 102, 193, 189, 190, 121, 100, 108, 167, 44,  43,  77,  180, 204, 8,   81,  70,  223, 11,  38,  24,  254, 210, 210, 177, 32,  81,  195, 243, 125, 8,   169, 112, 32,  97,  53,  195, 13,
                     203, 9,   47,  104, 125, 117, 114, 124, 165, 203, 181, 235, 193, 206, 70,  180, 174, 0,   167, 181, 41,  164, 30,  116, 127, 198, 245, 146, 87,  224, 149, 206, 57,  4,   192, 210, 65,  210, 129, 240, 178, 105, 228,
                     108, 245, 148, 140, 40,  35,  195, 38,  58,  65,  207, 215, 253, 65,  85,  208, 76,  62,  3,   237, 55,  89,  232, 50,  217, 64,  244, 157, 199, 121, 252, 90,  17,  212, 203, 149, 152, 140, 187, 234, 177, 73,  174,
                     193, 100, 192, 143, 97,  53,  145, 135, 19,  103, 13,  90,  135, 151, 199, 91,  239, 247, 33,  39,  145, 101, 120, 99,  3,   186, 86,  99,  41,  237, 203, 111, 79,  220, 135, 158, 42,  30,  154, 120, 67,  87,  167,
                     135, 176, 183, 191, 253, 115, 184, 21,  233, 58,  129, 233, 142, 39,  128, 211, 118, 137, 139, 255, 114, 20,  218, 113, 154, 27,  127, 246, 250, 1,   8,   198, 250, 209, 92,  222, 173, 21,  88,  102, 219};

int noise2( int x, int y, int seed ) {
    int tmp = hash[ ( unsigned int )( y + seed ) % 256 ];
    return hash[ ( unsigned int )( tmp + x ) % 256 ];
}

float lin_inter( float x, float y, float s ) {
    return x + s * ( y - x );
}

float smooth_inter( float x, float y, float s ) {
    return lin_inter( x, y, s * s * ( 3 - 2 * s ) );
}

float noise2d( float x, float y, int seed ) {
    int x_int = floor( x );
    int y_int = floor( y );
    float x_frac = x - x_int;
    float y_frac = y - y_int;
    int s = noise2( x_int, y_int, seed );
    int t = noise2( x_int + 1, y_int, seed );
    int u = noise2( x_int, y_int + 1, seed );
    int v = noise2( x_int + 1, y_int + 1, seed );
    float low = smooth_inter( s, t, x_frac );
    float high = smooth_inter( u, v, x_frac );
    return smooth_inter( low, high, y_frac );
}

float perlin2d( float x, float y, float freq, int depth, int seed ) {
    float xa = x * freq;
    float ya = y * freq;
    float amp = 1.0;
    float fin = 0;
    float div = 0.0;

    int i;
    for ( i = 0; i < depth; i++ ) {
        div += 256 * amp;
        fin += noise2d( xa, ya, seed ) * amp;
        amp /= 2;
        xa *= 2;
        ya *= 2;
    }

    return fin / div;
}

float perlin_noise_hills( int x, int z ) {
    float noise = perlin2d( x, z, 0.02f, 3, MAP_SEED );
    return ( noise - 0.5f ) * 15;
}

float perlin_noise_ground_noise( int x, int z ) {
    float noise = perlin2d( x, z, 0.1f, 2, MAP_SEED + 1 );
    return ( noise - 0.5f ) * 2;
}

float perlin_noise_level( int x, int z ) {
    float noise_orig = perlin2d( x, z, 0.004f, 2, MAP_SEED + 5 );
    noise_orig = ( noise_orig - 0.5f ) * 10;
    float noise = fabs( noise_orig );
    noise = noise * noise_orig;
    noise = noise > 1 ? 1 : noise;
    noise = noise < -1 ? -1 : noise;

    return noise * 10;
}

float perlin_noise_mountians( int x, int z ) {
    float noise = perlin2d( x, z, 0.008f, 3, MAP_SEED + 2 );
    noise = noise - 0.5f;
    if ( noise < 0 ) {
        noise = 0;
    }
    float mountians = noise * noise * noise * 1000;
    return mountians;
}

float perlin_noise_mountian_block( int x, int z ) {
    float noise = perlin2d( x, z, 0.8f, 8, MAP_SEED + 3 );
    return noise;
}

float perlin_noise_under_water_block( int x, int z ) {
    float noise = perlin2d( x, z, 0.2f, 2, MAP_SEED + 4 );
    return noise;
}
