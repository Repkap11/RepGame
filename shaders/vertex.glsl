#version 440 core
uniform vec4 u_CameraPosition;
uniform vec4 u_CameraUnit;
uniform vec4 u_LightPosition;
uniform mat4 u_MVP;

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 texCoord;

out vec2 v_TexCoord;

void main( ) {
    gl_Position =  u_MVP * position;
    v_TexCoord = texCoord;
}
