#version 310 es
uniform mat4 u_MVP;

// See SkyVertex in sky_box.hpp
layout( location = 0 ) in vec2 position;

void main( ) {
    gl_Position = u_MVP * vec4( position, -0.1, 1 );
}
