#version 300 es

precision highp float;
precision lowp sampler2DArray;

uniform vec4 u_Color;
layout( location = 0 ) out vec4 color;

void main( ) {
    color = u_Color;
}
