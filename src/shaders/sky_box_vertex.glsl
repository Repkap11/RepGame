#version 300 es
uniform mat4 u_MVP;

// See SkyVertex in sky_box.hpp
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoords;
layout( location = 2 ) in float blockID;
layout( location = 3 ) in mat4 transform;

out vec2 v_tex_coords;
flat out float v_blockID;

void main( ) {
    gl_Position = u_MVP * transform * vec4( position, 1 );
    v_tex_coords = texCoords;
    v_blockID = blockID;
}
