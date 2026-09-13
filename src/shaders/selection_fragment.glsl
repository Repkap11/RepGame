#version 300 es

// Screen-space wireframe selection outline.
//
// The cube is rendered as solid triangles (all 6 faces). In the fragment
// shader we compute the distance from the fragment to the nearest edge of
// the face it belongs to, convert that to pixels with fwidth, and discard
// any fragment that is more than u_LineWidth pixels away from an edge.
// This produces a constant-width anti-aliased outline without any
// deprecated GL_LINES / glLineWidth calls, and works consistently across
// all GPUs and OpenGL core-profile contexts.

precision highp float;

uniform float u_LineWidth;

in vec3 v_local_pos;
flat in uint v_faceType;

layout( location = 0 ) out vec4 color;
layout( location = 1 ) out vec4 reflection;
#if !defined( REPGAME_LOW_GRAPHICS )
layout( location = 2 ) out vec4 fogFactor;
layout( location = 3 ) out vec4 skyColor;
#endif

#define SELECTION_SIZE_OFFSET 0.0003f
#define FACE_TOP 0u
#define FACE_BOTTOM 1u
#define FACE_RIGHT 2u
#define FACE_FRONT 3u
#define FACE_LEFT 4u
#define FACE_BACK 5u

void main( ) {
    float s = SELECTION_SIZE_OFFSET;
    float lo = -s;
    float hi = 1.0f + s;

    // Select the two face-plane axes based on which face this fragment is on.
    vec2 face_pos;
    if ( v_faceType == FACE_TOP || v_faceType == FACE_BOTTOM ) {
        face_pos = v_local_pos.xz;
    } else if ( v_faceType == FACE_RIGHT || v_faceType == FACE_LEFT ) {
        face_pos = v_local_pos.zy;
    } else { // FACE_FRONT, FACE_BACK
        face_pos = v_local_pos.xy;
    }

    // Distance (in local block space) to the nearest edge along each axis.
    float dist_u = min( face_pos.x - lo, hi - face_pos.x );
    float dist_v = min( face_pos.y - lo, hi - face_pos.y );

    // Convert local-space distance to screen-space pixels using fwidth,
    // which gives the rate of change per pixel. A small epsilon avoids
    // division by zero for degenerate / head-on fragments.
    float pw_u = fwidth( face_pos.x ) + 0.00001f;
    float pw_v = fwidth( face_pos.y ) + 0.00001f;
    float pixel_dist_u = dist_u / pw_u;
    float pixel_dist_v = dist_v / pw_v;
    float pixel_dist = min( pixel_dist_u, pixel_dist_v );

    float lineWidth = u_LineWidth;
    if ( pixel_dist > lineWidth ) {
        discard;
    }

    // 1-pixel anti-aliased edge.
    float alpha = 1.0f - smoothstep( lineWidth - 1.0f, lineWidth, pixel_dist );

    vec3 lineColor = vec3( 0.0f, 0.0f, 0.0f );
    color = vec4( lineColor, alpha );
    reflection = vec4( 0.0f, 0.0f, 0.0f, 0.0f );
#if !defined( REPGAME_LOW_GRAPHICS )
    fogFactor = vec4( 0.0f, 0.0f, 0.0f, 0.0f );
    skyColor = vec4( 0.0f, 0.0f, 0.0f, 0.0f );
#endif
}
