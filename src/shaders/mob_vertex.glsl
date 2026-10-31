#version 300 es
uniform mat4 u_MVP;
uniform float u_ReflectionHeight;
uniform vec3 u_Origin;

#define FACE_TOP 0u
#define FACE_BOTTOM 1u
#define FACE_RIGHT 2u
#define FACE_FRONT 3u
#define FACE_LEFT 4u
#define FACE_BACK 5u

// See ParticleVertex in Particle.hpp (same unit-cube data as object_vertex).
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoords;
layout( location = 2 ) in uint faceType;

// See MobPartInstance in Particle.hpp. The mat4 takes locations 3-6.
layout( location = 3 ) in mat4 transform;
layout( location = 7 ) in vec2 instance_light;
layout( location = 8 ) in vec4 uv_a; // u, v, w, h in skin pixels
layout( location = 9 ) in vec4 uv_b; // d, skin_layer, mirror, flash

out vec2 v_tex_coords;
flat out vec4 v_tex_bounds;
out float v_light;
flat out vec2 v_light_field;
flat out float v_blockID;
flat out float v_id;
flat out float v_flash;
out float v_planarDot;
out vec4 v_world_coords;

// Bedrock box-uv unwrap — must match mob_face_rect in mob_uv.hpp. The unit
// cube's z=0 verts (FACE_BACK) are the model's -Z front; z=1 (FACE_FRONT) is
// the back. mirror swaps the left/right rects.
vec4 face_rect( uint face ) {
    float u = uv_a.x, v = uv_a.y, w = uv_a.z, h = uv_a.w, d = uv_b.x;
    bool mirror = uv_b.z != 0.0;
    if ( face == FACE_TOP )
        return vec4( u + d, v, w, d );
    if ( face == FACE_BOTTOM )
        return vec4( u + d + w, v, w, d );
    if ( face == FACE_RIGHT )
        return vec4( mirror ? u + d + w : u, v + d, d, h );
    if ( face == FACE_LEFT )
        return vec4( mirror ? u : u + d + w, v + d, d, h );
    if ( face == FACE_FRONT )
        return vec4( u + 2.0 * d + w, v + d, w, h );
    return vec4( u + d, v + d, w, h );
}

void main( ) {
    v_world_coords = transform * vec4( position, 1 ) - vec4( u_Origin, 0.0 );
    gl_Position = u_MVP * v_world_coords;
    v_planarDot = dot( v_world_coords, vec4( 0, 1, 0, u_ReflectionHeight ) );

    // In-rect fractions: the vd_data_player_object texCoords already run the
    // right way around each face horizontally (fu = texCoords.x). Skin v grows
    // downward, so vertical faces take 1 - v; the bottom face's rect is read
    // top-edge = front edge, so it takes v directly.
    vec4 rect = face_rect( faceType );
    float fu = uv_b.z != 0.0 ? 1.0 - texCoords.x : texCoords.x;
    float fv = faceType == FACE_BOTTOM ? texCoords.y : 1.0 - texCoords.y;
    float px = rect.x + fu * rect.z;
    float py = rect.y + fv * rect.w;
    // Loader mapping: decode flips the image vertically and loadTexture
    // mirrors each tile in X, so skin pixel (px, py) samples at
    // (1 - px/64, 1 - py/64).
    v_tex_coords = vec2( 1.0 - px / 64.0, 1.0 - py / 64.0 );

    // Face rect bounds in texture space (same flip as above), inset by half
    // a texel: MSAA evaluates edge fragments off-triangle, and without the
    // clamp their extrapolated v_tex_coords sample neighbouring skin texels.
    vec2 tex_lo = vec2( 1.0 - ( rect.x + rect.z ) / 64.0, 1.0 - ( rect.y + rect.w ) / 64.0 );
    vec2 tex_hi = vec2( 1.0 - rect.x / 64.0, 1.0 - rect.y / 64.0 );
    tex_lo += vec2( 0.5 / 64.0 );
    tex_hi = max( tex_hi - vec2( 0.5 / 64.0 ), tex_lo );
    v_tex_bounds = vec4( tex_lo, tex_hi );

    float face_light;
    if ( faceType == FACE_TOP ) {
        face_light = 1.0;
    } else if ( faceType == FACE_BOTTOM ) {
        face_light = 0.5;
    } else {
        face_light = 0.75;
    }
    v_light = face_light;
    v_light_field = instance_light;
    v_blockID = uv_b.y;
    // Negative flash is the hurt red tint in object_fragment.
    v_flash = uv_b.w;
}
