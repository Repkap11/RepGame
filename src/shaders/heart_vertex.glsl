#version 300 es
uniform mat4 u_MVP;
uniform float u_ReflectionHeight;
uniform vec3 u_Origin;
uniform float u_Time;
// Camera basis in world space (rows of the view rotation): the billboard
// quad spans right/up so the sprite always faces the viewer.
uniform vec3 u_CamRight;
uniform vec3 u_CamUp;

// See ParticleVertex in Particle.hpp (same layout as object_vertex.glsl).
// The element mesh is the single vd_data_face_object quad.
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoords;

// Same instance layout as debris_vertex.glsl (DebrisInstance): velocity is
// the drift, texel is unused — the sprite maps the whole quad.
layout( location = 3 ) in uvec3 blockTexture;
layout( location = 4 ) in vec3 spawnPos;
layout( location = 5 ) in vec3 velocity;
layout( location = 6 ) in vec4 anim; // x=spawn time, y=life, z=size, w=seed
// Per-instance flood-fill light at the mob (x=sky, y=block, 0-1).
layout( location = 7 ) in vec2 instance_light;

out vec2 v_tex_coords;
// object_fragment clamps to this bound (mob faces only);
// a huge rect leaves this program's sampling untouched.
flat out vec4 v_tex_bounds;
out float v_light;
flat out vec2 v_light_field;
flat out float v_blockID;
flat out float v_flash;
out float v_planarDot;
out vec4 v_world_coords;

// Sideways sway amplitude grows over the lifetime (blocks).
#define HEART_SWAY 0.08f
// Fraction of lifetime spent popping in / shrinking out.
#define HEART_GROW_END 0.15f
#define HEART_SHRINK_START 0.7f
// u_Time is fmod(now, 3600) in RepGame::draw; keep ages positive across the wrap.
#define TIME_WRAP 3600.0f

void main( ) {
    float age = u_Time - anim.x;
    // Only a clock rollover wraps; a small negative age means the particle
    // was spawned mid-frame after u_Time was captured — it is not yet born.
    if ( age < -TIME_WRAP * 0.5f ) {
        age += TIME_WRAP;
    }
    age = max( age, 0.0f );
    float life_f = clamp( age / anim.y, 0.0f, 1.0f );

    // Gentle rise + a per-particle sideways sway; no gravity, no tumble.
    float seed = anim.w;
    vec3 sway = vec3( sin( age * 3.0f + seed ), 0.0f, cos( age * 2.7f + seed * 1.3f ) ) * ( HEART_SWAY * age );
    vec3 center = spawnPos + velocity * age + sway;

    // Pop in over the first 15% of the life, shrink out over the last third.
    float scale = anim.z * smoothstep( 0.0f, HEART_GROW_END, life_f ) * ( 1.0f - smoothstep( HEART_SHRINK_START, 1.0f, life_f ) );

    // Billboard: the face quad's x/y span the camera's right/up axes. The
    // index buffer's winding stays front-facing only while y maps to
    // -screen-up (this exact quad is also what debris draws); the v flip
    // below restores an upright sprite — stb's vertical flip puts the image
    // top at v=1, and the quad's y=0 vert carries v=0.
    vec3 local = u_CamRight * ( position.x - 0.5f ) * scale + u_CamUp * ( 0.5f - position.y ) * scale;
    v_world_coords = vec4( center + local, 1.0f ) - vec4( u_Origin, 0.0f );
    gl_Position = u_MVP * v_world_coords;
    v_planarDot = dot( v_world_coords, vec4( 0, 1, 0, u_ReflectionHeight ) );

    v_tex_coords = vec2( texCoords.x, 1.0f - texCoords.y );
    v_tex_bounds = vec4( -1e6, -1e6, 1e6, 1e6 );
    // A floating sprite shades like the top face (flat, brightest).
    v_light = 1.0;
    v_light_field = instance_light;
    v_flash = 0.0f;
    v_blockID = float( blockTexture.x & 0xffffu );
}
