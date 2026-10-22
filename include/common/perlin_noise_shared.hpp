#pragma once

// Single-source perlin noise for every map_gen backend. Written in the
// C++/WGSL dual dialect (include/common/wgsl_dialect.hpp): the includer
// defines MG_QUAL ("static inline"/"" on host, __device__ on CUDA/HIP) and
// the same text is also preprocessed with -DREPGAME_WGSL_GEN into the WGSL
// mapgen shader. Previously three hand-synced copies existed
// (perlin_noise.cpp, perlin_noise.cu, perlin_noise_impl.inl).
//
// Contract: this file produces noise2/noise3/noise2d/noise3d/lin_inter/
// smooth_inter/perlin_noise2d/perlin_noise3d with extern linkage on the CPU
// TU (perlin_noise.cpp) and device fns elsewhere. All return values are in
// [0, 1] and identical across backends (modulo float rounding).

#include "common/wgsl_dialect.hpp"

// The 256-entry permutation hash — same literal list in every backend via
// MG_HASH_VALUES so lookups stay identical.
#define MG_HASH_VALUES                                                                                                                                                            \
    208, 34, 231, 213, 32, 248, 233, 56, 161, 78, 24, 140, 71, 48, 140, 254, 245, 255, 247, 247, 40, 185, 248, 251, 245, 28, 124, 204, 204, 76, 36, 1, 107, 28, 234, 163, 202, 224, \
        245, 128, 167, 204, 9, 92, 217, 54, 239, 174, 173, 102, 193, 189, 190, 121, 100, 108, 167, 44, 43, 77, 180, 204, 8, 81, 70, 223, 11, 38, 24, 254, 210, 210, 177, 32, 81,      \
        195, 243, 125, 8, 169, 112, 32, 97, 53, 195, 13, 203, 9, 47, 104, 125, 117, 114, 124, 165, 203, 181, 235, 193, 206, 70, 180, 174, 0, 167, 181, 41, 164, 30, 116, 127, 198,    \
        245, 146, 87, 224, 149, 206, 57, 4, 192, 210, 65, 210, 129, 240, 178, 105, 228, 108, 245, 148, 140, 40, 35, 195, 38, 58, 65, 207, 215, 253, 65, 85, 208, 76, 62, 3, 237, 55,   \
        89, 232, 50, 217, 64, 244, 157, 199, 121, 252, 90, 17, 212, 203, 149, 152, 140, 187, 234, 177, 73, 174, 193, 100, 192, 143, 97, 53, 145, 135, 19, 103, 13, 90, 135, 151, 199,   \
        91, 239, 247, 33, 39, 145, 101, 120, 99, 3, 186, 86, 99, 41, 237, 203, 111, 79, 220, 135, 158, 42, 30, 154, 120, 67, 87, 167, 135, 176, 183, 191, 253, 115, 184, 21, 233, 58,   \
        129, 233, 142, 39, 128, 211, 118, 137, 139, 255, 114, 20, 218, 113, 154, 27, 127, 246, 250, 1, 8, 198, 250, 209, 92, 222, 173, 21, 88, 102, 219

#if defined( REPGAME_WGSL_GEN )
const hash = array<i32, 256>( MG_HASH_VALUES );
#else
// No `static` here: MG_QUAL may already carry it (the CUDA/HIP TUs need
// internal linkage so these don't collide with the extern CPU instantiation
// at link time), and namespace-scope const is internal-linkage anyway.
MG_QUAL const int hash[ 256 ] = { MG_HASH_VALUES };
#endif

MG_FN3( int, noise2, int, x, int, y, int, seed ) {
    MG_LET( int, tmp, MG_HASH( y + seed ) )
    return MG_HASH( tmp + x );
}

MG_FN4( int, noise3, int, x, int, y, int, z, int, seed ) {
    MG_LET( int, tmp, MG_HASH( y + seed ) )
    MG_LET( int, tmp2, MG_HASH( tmp + z ) )
    return MG_HASH( tmp2 + x );
}

MG_FN3( float, lin_inter, float, x, float, y, float, s ) {
    return x + s * ( y - x );
}

MG_FN3( float, smooth_inter, float, x, float, y, float, s ) {
    return lin_inter( x, y, s * s * ( 3 - 2 * s ) );
}

MG_FN3( float, noise2d, float, x, float, y, int, seed ) {
    MG_LET( int, x_int, MG_I32( MG_FLOOR( x ) ) )
    MG_LET( int, y_int, MG_I32( MG_FLOOR( y ) ) )
    MG_LET( float, x_frac, x - MG_F32( x_int ) )
    MG_LET( float, y_frac, y - MG_F32( y_int ) )
    MG_LET( int, s, noise2( x_int, y_int, seed ) )
    MG_LET( int, t, noise2( x_int + 1, y_int, seed ) )
    MG_LET( int, u, noise2( x_int, y_int + 1, seed ) )
    MG_LET( int, v, noise2( x_int + 1, y_int + 1, seed ) )
    MG_LET( float, low, smooth_inter( MG_F32( s ), MG_F32( t ), x_frac ) )
    MG_LET( float, high, smooth_inter( MG_F32( u ), MG_F32( v ), x_frac ) )
    return smooth_inter( low, high, y_frac );
}

MG_FN4( float, noise3d, float, x, float, y, float, z, int, seed ) {
    MG_LET( int, x_int, MG_I32( MG_FLOOR( x ) ) )
    MG_LET( int, y_int, MG_I32( MG_FLOOR( y ) ) )
    MG_LET( int, z_int, MG_I32( MG_FLOOR( z ) ) )
    MG_LET( float, x_frac, x - MG_F32( x_int ) )
    MG_LET( float, y_frac, y - MG_F32( y_int ) )
    MG_LET( float, z_frac, z - MG_F32( z_int ) )

    MG_LET( int, o, noise3( x_int, y_int, z_int, seed ) )
    MG_LET( int, p, noise3( x_int + 1, y_int, z_int, seed ) )
    MG_LET( int, q, noise3( x_int, y_int + 1, z_int, seed ) )
    MG_LET( int, r, noise3( x_int + 1, y_int + 1, z_int, seed ) )

    MG_LET( float, low1, smooth_inter( MG_F32( o ), MG_F32( p ), x_frac ) )
    MG_LET( float, high1, smooth_inter( MG_F32( q ), MG_F32( r ), x_frac ) )
    MG_LET( float, down, smooth_inter( low1, high1, y_frac ) )

    MG_LET( int, s, noise3( x_int, y_int, z_int + 1, seed ) )
    MG_LET( int, t, noise3( x_int + 1, y_int, z_int + 1, seed ) )
    MG_LET( int, u, noise3( x_int, y_int + 1, z_int + 1, seed ) )
    MG_LET( int, v, noise3( x_int + 1, y_int + 1, z_int + 1, seed ) )

    MG_LET( float, low2, smooth_inter( MG_F32( s ), MG_F32( t ), x_frac ) )
    MG_LET( float, high2, smooth_inter( MG_F32( u ), MG_F32( v ), x_frac ) )
    MG_LET( float, up, smooth_inter( low2, high2, y_frac ) )
    return smooth_inter( down, up, z_frac );
}

MG_FN5( float, perlin_noise2d, float, x, float, y, float, freq, int, depth, int, seed ) {
    MG_VAR( float, xa, x * freq )
    MG_VAR( float, ya, y * freq )
    MG_VAR( float, amp, 1.0 )
    MG_VAR( float, fin, 0 )
    MG_VAR( float, div, 0.0 )
    MG_FOR( i, 0, i < depth, i++ ) {
        div += 256 * amp;
        fin += noise2d( xa, ya, seed ) * amp;
        amp /= 2;
        xa *= 2;
        ya *= 2;
    }
    return fin / div;
}

MG_FN6( float, perlin_noise3d, float, x, float, y, float, z, float, freq, int, depth, int, seed ) {
    MG_VAR( float, xa, x * freq )
    MG_VAR( float, ya, y * freq )
    MG_VAR( float, za, z * freq )
    MG_VAR( float, amp, 1.0 )
    MG_VAR( float, fin, 0 )
    MG_VAR( float, div, 0.0 )
    MG_FOR( i, 0, i < depth, i++ ) {
        div += 256 * amp;
        fin += noise3d( xa, ya, za, seed ) * amp;
        amp /= 2;
        xa *= 2;
        ya *= 2;
        za *= 2;
    }
    return fin / div;
}
