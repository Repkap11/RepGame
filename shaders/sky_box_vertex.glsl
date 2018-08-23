#version 310 es

// See SkyVertex in sky_box.hpp
layout( location = 0 ) in vec3 position;

void main( ) {
    gl_Position = vec4( position, 1 );
}
