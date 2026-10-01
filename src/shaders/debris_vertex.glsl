#version 300 es
uniform mat4 u_MVP;
uniform float u_ReflectionHeight;
uniform vec3 u_Origin;
uniform float u_Time;

#define FACE_TOP 0u
#define FACE_BOTTOM 1u

// See ParticleVertex in Particle.hpp (same layout as object_vertex.glsl)
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoords;
layout( location = 2 ) in uint faceType;

// See DebrisInstance in Particle.hpp
layout( location = 3 ) in uvec3 blockTexture;
layout( location = 4 ) in vec3 spawnPos;
layout( location = 5 ) in vec3 velocity;
layout( location = 6 ) in vec4 anim; // x=spawn time, y=life, z=size, w=seed

out vec2 v_tex_coords;
out float v_light;
flat out float v_blockID;
out float v_planarDot;
out vec4 v_world_coords;

#define DEBRIS_GRAVITY 9.8f
// Tumble speed range in radians/second (per-particle speed picked by seed).
#define DEBRIS_ROT_SPEED_MIN 4.0f
#define DEBRIS_ROT_SPEED_SPREAD 6.0f
// Fraction of lifetime at which the particle starts shrinking out.
#define DEBRIS_SHRINK_START 0.6f
// Block texture tiles are 16x16 (MK_TEXTURE in RepGame.cpp); particles sample
// a single texel so MAG_NEAREST gives a clean flat color.
#define DEBRIS_TEXTURE_PIXELS 16.0f
// u_Time is fmod(now, 3600) in RepGame::draw; keep ages positive across the wrap.
#define TIME_WRAP 3600.0f

void main( ) {
    float age = u_Time - anim.x;
    if ( age < 0.0f ) {
        age += TIME_WRAP;
    }
    float life_f = clamp( age / anim.y, 0.0f, 1.0f );

    // Ballistic flight: launch velocity plus gravity.
    vec3 center = spawnPos + velocity * age + vec3( 0.0f, -0.5f * DEBRIS_GRAVITY, 0.0f ) * ( age * age );

    // Tumble: axis-angle rotation around a per-particle axis hashed from the seed.
    float seed = anim.w;
    vec3 axis = vec3( fract( seed * 0.1031f ), fract( seed * 0.09769f ), fract( seed * 0.09357f ) ) - 0.5f;
    axis = normalize( axis + vec3( 0.001f ) ); // bias keeps the axis off zero
    float angle = seed + age * ( DEBRIS_ROT_SPEED_MIN + fract( seed * 0.0713f ) * DEBRIS_ROT_SPEED_SPREAD );
    float c = cos( angle );
    float s = sin( angle );
    float ic = 1.0f - c;
    mat3 tumble = mat3( axis.x * axis.x * ic + c, //
                        axis.x * axis.y * ic - axis.z * s,
                        axis.x * axis.z * ic + axis.y * s, //
                        axis.y * axis.x * ic + axis.z * s,
                        axis.y * axis.y * ic + c, //
                        axis.y * axis.z * ic - axis.x * s, //
                        axis.z * axis.x * ic - axis.y * s,
                        axis.z * axis.y * ic + axis.x * s, //
                        axis.z * axis.z * ic + c );

    // Shrink out over the last third of the lifetime. At age >= life the scale
    // reaches 0 and the cube degenerates to a point until the CPU sweeps it.
    float scale = anim.z * ( 1.0f - smoothstep( DEBRIS_SHRINK_START, 1.0f, life_f ) );
    vec3 local = tumble * ( ( position - 0.5f ) * scale );
    v_world_coords = vec4( center + local, 1.0f ) - vec4( u_Origin, 0.0f );
    gl_Position = u_MVP * v_world_coords;
    v_planarDot = dot( v_world_coords, vec4( 0, 1, 0, u_ReflectionHeight ) );

    // Instead of the block's texture, each particle shows one solid color: a
    // single texel of a randomly chosen face texture, identical for every
    // vertex of the instance.
    v_tex_coords = ( floor( vec2( fract( seed * 0.2317f ), fract( seed * 0.7193f ) ) * DEBRIS_TEXTURE_PIXELS ) + 0.5f ) / DEBRIS_TEXTURE_PIXELS;
    uint pickFace = uint( fract( seed * 0.4171f ) * 6.0f ); // one of the 6 face textures
    pickFace = min( pickFace, 5u );
    uint pickShift = ( pickFace % 2u ) * 16u;
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
    v_blockID = float( ( blockTexture[ pickFace / 2u ] & ( 0xffffu << pickShift ) ) >> pickShift );
}
