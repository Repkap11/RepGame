#version 440 core
uniform mat4 u_MVP;

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec3 texCoordBlock;

out vec3 v_TexCoordBlock;

void main( ) {
    gl_Position =  u_MVP * position;
    v_TexCoordBlock = texCoordBlock;
}
