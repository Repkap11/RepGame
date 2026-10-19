#version 300 es

#include "common/chunk_dimensions.hpp"

// Chunk-AABB occlusion probe: unit-cube vertices ([0,1]^3) scaled to the
// chunk's block footprint and moved to its world corner, rebased like the
// chunk shader's u_Origin. u_BoxMin = chunk_pos*CHUNK_SIZE - render_origin.
uniform mat4 u_MVP;
uniform vec3 u_BoxMin;

layout( location = 0 ) in vec3 position;

void main( ) {
    vec3 p = u_BoxMin + position * vec3( float( CHUNK_SIZE_X ), float( CHUNK_SIZE_Y ), float( CHUNK_SIZE_Z ) );
    gl_Position = u_MVP * vec4( p, 1.0 );
}
