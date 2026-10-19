#version 300 es

// Occlusion probes run with all color writes masked, so this value is a
// don't-care — the fragment stage still needs a valid program.
precision highp float;

layout( location = 0 ) out vec4 color;

void main( ) {
    color = vec4( 0.0 );
}
