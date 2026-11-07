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

// See ParticleVertex in Particle.hpp (same layout as object_vertex.glsl)
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoords;
layout( location = 2 ) in uint faceType;

// See BlockObjectInstance in Particle.hpp. The mat4 takes locations 4-7.
layout( location = 3 ) in uvec3 blockTexture;
layout( location = 4 ) in mat4 transform;
// Per-instance flood-fill light sampled on the CPU (x=sky, y=block, 0-1).
layout( location = 8 ) in vec2 instance_light;
// The block's pixel-space (0-16) shape: the unit cube's faces collapse to the
// sub-box, and each face's UV window crops to the drawn region exactly like
// chunk_vertex.glsl's unrotated scale/offset/tex_offset path.
layout( location = 9 ) in vec3 shape_scale;
layout( location = 10 ) in vec3 shape_offset;
layout( location = 11 ) in vec3 shape_tex_offset;

out vec2 v_tex_coords;
// object_fragment clamps to this bound: the face's cropped UV region inset
// by half a texel (same edge-clamp idea as chunk_fragment's
// v_tex_min/v_tex_max) so bilinear taps never bleed the neighbouring
// transparent texels a non-full shape sits next to.
flat out vec4 v_tex_bounds;
out float v_light;
flat out vec2 v_light_field;
flat out float v_blockID;
flat out float v_flash;
out float v_planarDot;
out vec4 v_world_coords;

void main( ) {
    vec3 s = shape_scale / 16.0;
    vec3 o = shape_offset / 16.0;
    vec3 t = shape_tex_offset / 16.0;
    vec3 p = position * s + o;
    v_world_coords = transform * vec4( p, 1 ) - vec4( u_Origin, 0.0 );
    gl_Position = u_MVP * v_world_coords;
    v_planarDot = dot( v_world_coords, vec4( 0, 1, 0, u_ReflectionHeight ) );

    vec2 face_scale;
    vec2 face_shift;
    if ( faceType == FACE_TOP || faceType == FACE_BOTTOM ) {
        face_scale = s.xz;
        face_shift = t.xz;
    } else if ( faceType == FACE_RIGHT || faceType == FACE_LEFT ) {
        face_scale = s.zy;
        face_shift = t.xy;
    } else { // FACE_FRONT, FACE_BACK
        face_scale = s.xy;
        face_shift = t.zy;
    }
    v_tex_coords = texCoords * face_scale - face_shift;
    vec2 tex_lo = -face_shift + vec2( 0.5 / 16.0 );
    vec2 tex_hi = max( face_scale - face_shift - vec2( 0.5 / 16.0 ), tex_lo );
    v_tex_bounds = vec4( tex_lo, tex_hi );

    uint shift = ( faceType % 2u ) * 16u;
    // 0xffffu is max short 16u is sizeof(short)

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
    // Never flashes; primed TNT does (primed_tnt_vertex).
    v_flash = 0.0f;
    v_blockID = float( ( blockTexture[ faceType / 2u ] & ( 0xffffu << shift ) ) >> shift );
}
