// ---------------------------------------------------------------------------
// end_sky_glsl.hpp — shared GLSL for the End dimension's two noise effects.
// Included (cpp preprocessing) by object_fragment.glsl and chunk_fragment.glsl.
//
// end_sky_cube_uv: the End sky is vanilla's inverted cube — the noise tiles
// 16x per face edge, dominant-axis mapped. Noise has no spatial correlation,
// so the face-orientation rotations vanilla applies are pointless here and
// the seams are invisible even where the mapping is discontinuous.
//
// END_PORTAL_LAYER_COLORS + end_portal_layer_uv: the screen-space starfield
// of end portal/gateway blocks, ported from vanilla's
// rendertype_end_portal.fsh — 15 additive layers (16th blue for gateways)
// of the sparse star texture over a dark noise background, each layer with
// its own scale/rotation/x-phase and a slow upward scroll.
// ---------------------------------------------------------------------------
#ifndef REPGAME_END_SKY_GLSL_HPP
#define REPGAME_END_SKY_GLSL_HPP

// This header is cpp-included near the top of the fragment shaders, before
// their own precision statements — float precision must be pinned here or
// ES rejects the vec types below. Redeclaring it later is legal.
precision highp float;

vec2 end_sky_cube_uv( vec3 dir ) {
    vec3 a = abs( dir );
    vec2 uv;
    if ( a.x >= a.y && a.x >= a.z ) {
        uv = dir.zy / a.x;
    } else if ( a.y >= a.z ) {
        uv = dir.xz / a.y;
    } else {
        uv = dir.xy / a.z;
    }
    // Each component pair spans [-1,1] across the face; 8 units per
    // half-edge tiles the noise 16 times edge to edge like vanilla.
    return uv * 8.0;
}

// Vanilla rendertype_end_portal layer tints: index 0 is both the dark teal
// of the noise background and the first star layer's tint; the last one is
// the end gateway's extra blue layer.
const vec3 END_PORTAL_LAYER_COLORS[ 16 ] = vec3[]( //
    vec3( 0.022087, 0.098399, 0.110818 ),          //
    vec3( 0.011892, 0.095924, 0.089485 ),          //
    vec3( 0.027636, 0.101689, 0.100326 ),          //
    vec3( 0.046564, 0.109883, 0.114838 ),          //
    vec3( 0.064901, 0.117696, 0.097189 ),          //
    vec3( 0.063761, 0.086895, 0.123646 ),          //
    vec3( 0.084817, 0.111994, 0.166380 ),          //
    vec3( 0.097489, 0.154120, 0.091064 ),          //
    vec3( 0.106152, 0.131144, 0.195191 ),          //
    vec3( 0.097721, 0.110188, 0.187229 ),          //
    vec3( 0.133516, 0.138278, 0.148582 ),          //
    vec3( 0.070006, 0.243332, 0.235792 ),          //
    vec3( 0.196766, 0.142899, 0.214696 ),          //
    vec3( 0.047281, 0.315338, 0.321970 ),          //
    vec3( 0.204675, 0.390010, 0.302066 ),          //
    vec3( 0.080955, 0.314821, 0.661491 ) );

// Per-layer UV transform, in the order vanilla's matrix chain applies it:
// shrink toward the screen-center quarter, fixed x phase + upward scroll,
// then per-layer rotation+scale. layer is 1-based; game_time in the units
// vanilla's GameTime uses (one per 20 real minutes — u_Time is seconds).
vec2 end_portal_layer_uv( vec2 screen_uv, float layer, float game_time ) {
    vec2 uv = screen_uv * 0.5 + 0.25;
    uv += vec2( 17.0 / layer, ( 2.0 + layer / 1.5 ) * game_time * 1.5 );
    float scale = ( 4.5 - layer / 4.0 ) * 2.0;
    float ang = radians( ( layer * layer * 4321.0 + layer * 9.0 ) * 2.0 );
    float c = cos( ang );
    float s = sin( ang );
    uv = scale * vec2( uv.x * c + uv.y * s, -uv.x * s + uv.y * c );
    return uv;
}

#endif
