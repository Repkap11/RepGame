#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "common/perlin_noise.hpp"
#include "common/RepGame.hpp"
#include "common/constants.hpp"

constexpr static int hash[] = { 208, 34,  231, 213, 32,  248, 233, 56,  161, 78,  24,  140, 71,  48,  140, 254, 245, 255, 247, 247, 40,  185, 248, 251, 245, 28,  124, 204, 204, 76,  36,  1,   107, 28,  234, 163, 202,
                                224, 245, 128, 167, 204, 9,   92,  217, 54,  239, 174, 173, 102, 193, 189, 190, 121, 100, 108, 167, 44,  43,  77,  180, 204, 8,   81,  70,  223, 11,  38,  24,  254, 210, 210, 177, 32,
                                81,  195, 243, 125, 8,   169, 112, 32,  97,  53,  195, 13,  203, 9,   47,  104, 125, 117, 114, 124, 165, 203, 181, 235, 193, 206, 70,  180, 174, 0,   167, 181, 41,  164, 30,  116, 127,
                                198, 245, 146, 87,  224, 149, 206, 57,  4,   192, 210, 65,  210, 129, 240, 178, 105, 228, 108, 245, 148, 140, 40,  35,  195, 38,  58,  65,  207, 215, 253, 65,  85,  208, 76,  62,  3,
                                237, 55,  89,  232, 50,  217, 64,  244, 157, 199, 121, 252, 90,  17,  212, 203, 149, 152, 140, 187, 234, 177, 73,  174, 193, 100, 192, 143, 97,  53,  145, 135, 19,  103, 13,  90,  135,
                                151, 199, 91,  239, 247, 33,  39,  145, 101, 120, 99,  3,   186, 86,  99,  41,  237, 203, 111, 79,  220, 135, 158, 42,  30,  154, 120, 67,  87,  167, 135, 176, 183, 191, 253, 115, 184,
                                21,  233, 58,  129, 233, 142, 39,  128, 211, 118, 137, 139, 255, 114, 20,  218, 113, 154, 27,  127, 246, 250, 1,   8,   198, 250, 209, 92,  222, 173, 21,  88,  102, 219 };

int noise2( const int x, const int y, const int seed ) {
    const int tmp = hash[ static_cast<unsigned int>( y + seed ) % 256 ];
    return hash[ static_cast<unsigned int>( tmp + x ) % 256 ];
}

int noise3( const int x, const int y, const int z, const int seed ) {
    const int tmp = hash[ static_cast<unsigned int>( y + seed ) % 256 ];
    const int tmp2 = hash[ static_cast<unsigned int>( tmp + z ) % 256 ];
    return hash[ static_cast<unsigned int>( tmp2 + x ) % 256 ];
}

float lin_inter( const float x, const float y, const float s ) {
    return x + s * ( y - x );
}

float smooth_inter( float x, float y, float s ) {
    return lin_inter( x, y, s * s * ( 3 - 2 * s ) );
}

float noise2d( float x, float y, int seed ) {
    const int x_int = floor( x );
    const int y_int = floor( y );
    const float x_frac = x - x_int;
    const float y_frac = y - y_int;
    const int s = noise2( x_int, y_int, seed );
    const int t = noise2( x_int + 1, y_int, seed );
    const int u = noise2( x_int, y_int + 1, seed );
    const int v = noise2( x_int + 1, y_int + 1, seed );
    const float low = smooth_inter( s, t, x_frac );
    const float high = smooth_inter( u, v, x_frac );
    return smooth_inter( low, high, y_frac );
}

float noise3d( const float x, const float y, const float z, const int seed ) {
    const int x_int = floor( x );
    const int y_int = floor( y );
    const int z_int = floor( z );
    const float x_frac = x - x_int;
    const float y_frac = y - y_int;
    const float z_frac = z - z_int;

    const int o = noise3( x_int, y_int, z_int, seed );
    const int p = noise3( x_int + 1, y_int, z_int, seed );
    const int q = noise3( x_int, y_int + 1, z_int, seed );
    const int r = noise3( x_int + 1, y_int + 1, z_int, seed );

    const float low1 = smooth_inter( o, p, x_frac );
    const float high1 = smooth_inter( q, r, x_frac );
    const float down = smooth_inter( low1, high1, y_frac );

    const int s = noise3( x_int, y_int, z_int + 1, seed );
    const int t = noise3( x_int + 1, y_int, z_int + 1, seed );
    const int u = noise3( x_int, y_int + 1, z_int + 1, seed );
    const int v = noise3( x_int + 1, y_int + 1, z_int + 1, seed );

    const float low2 = smooth_inter( s, t, x_frac );
    const float high2 = smooth_inter( u, v, x_frac );
    const float up = smooth_inter( low2, high2, y_frac );
    return smooth_inter( down, up, z_frac );
}

float perlin_noise2d( const float x, const float y, const float freq, const int depth, const int seed ) {
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

float perlin_noise3d( const float x, const float y, const float z, const float freq, const int depth, const int seed ) {
    float xa = x * freq;
    float ya = y * freq;
    float za = z * freq;
    float amp = 1.0;
    float fin = 0;
    float div = 0.0;

    for ( int i = 0; i < depth; i++ ) {
        div += 256 * amp;
        fin += noise3d( xa, ya, za, seed ) * amp;
        amp /= 2;
        xa *= 2;
        ya *= 2;
        za *= 2;
    }

    return fin / div;
}