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

// See PrimedTntInstance in Particle.hpp
layout( location = 3 ) in uvec3 blockTexture;
layout( location = 4 ) in vec3 spawnPos;
layout( location = 5 ) in vec3 velocity;
layout( location = 6 ) in vec4 anim; // x=spawn time, y=life, z=ground-clamp time, w=seed
// Per-instance flood-fill light at the ignited cell (x=sky, y=block, 0-1).
layout( location = 7 ) in vec2 instance_light;

out vec2 v_tex_coords;
out float v_light;
flat out vec2 v_light_field;
flat out float v_blockID;
flat out float v_flash;
out float v_planarDot;
out vec4 v_world_coords;

#define TNT_GRAVITY 9.8f
// Slightly under a full block like MC's primed entity.
#define TNT_SIZE 0.98f
// White flash toggles every ~0.25s, matching MC's fuse%10<5 blink.
#define TNT_FLASH_HZ 4.0f
// u_Time is fmod(now, 3600) in RepGame::draw; keep ages positive across the wrap.
#define TIME_WRAP 3600.0f

void main( ) {
    float age = u_Time - anim.x;
    if ( age < 0.0f ) {
        age += TIME_WRAP;
    }

    // Ballistic flight clamped at the CPU-predicted ground-impact time: the
    // cube lands on terrain and rests there until the fuse sweeps it.
    float t = min( age, anim.z );
    vec3 center = spawnPos + velocity * t + vec3( 0.0f, -0.5f * TNT_GRAVITY, 0.0f ) * ( t * t );
    vec3 local = ( position - 0.5f ) * TNT_SIZE;
    v_world_coords = vec4( center + local, 1.0f ) - vec4( u_Origin, 0.0f );
    gl_Position = u_MVP * v_world_coords;
    v_planarDot = dot( v_world_coords, vec4( 0, 1, 0, u_ReflectionHeight ) );

    v_tex_coords = texCoords;
    v_flash = mod( floor( age * TNT_FLASH_HZ ), 2.0f );

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
    v_blockID = float( ( blockTexture[ faceType / 2u ] & ( 0xffffu << shift ) ) >> shift );
}
