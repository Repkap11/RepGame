#version 300 es

#include "common/chunk_dimensions.hpp"

// Chunk-AABB occlusion probe: unit-cube vertices ([0,1]^3) scaled to the
// chunk's block footprint and moved to its world corner, rebased like the
// chunk shader's u_Origin. u_BoxMin = chunk_pos*CHUNK_SIZE - render_origin.
// The box is inflated by OCCL_BOX_EPS blocks: the opaque pass grows faces
// ~0.5px in screen space (u_FaceGrowPx, chunk_vertex.glsl), which tilts the
// grown quads' interpolated depth — a wall flush with the chunk boundary
// can then rasterize slightly nearer than a non-inflated box face and
// "occlude" its own probe: the chunk hides for a frame, the uncovered box
// passes the next probe, and the loop repeats as flicker. The epsilon also
// covers outset geometry that pokes a few 16ths past the boundary. Anything
// under 1 block keeps flush-neighbor occluders (nearest face is 1 block in
// front of the box plane) hiding correctly.
#define OCCL_BOX_EPS 0.25
uniform mat4 u_MVP;
uniform vec3 u_BoxMin;

layout( location = 0 ) in vec3 position;

void main( ) {
    vec3 p = u_BoxMin + position * vec3( float( CHUNK_SIZE_X ), float( CHUNK_SIZE_Y ), float( CHUNK_SIZE_Z ) ) + ( position - 0.5 ) * ( 2.0 * OCCL_BOX_EPS );
    gl_Position = u_MVP * vec4( p, 1.0 );
}
