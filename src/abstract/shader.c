#define GL_GLEXT_PROTOTYPES
#include "abstract/shader.h"

#include "shaders.h"

void shader_init( Shader *shader ) {
    shader->program = shaders_compile( "shaders/vertex.glsl", "shaders/fragment.glsl" );
}

void shader_bind( const Shader *shader ) {
    glUseProgram( shader->program );
}

void shader_unbind( const Shader *shader ) {
    glUseProgram( 0 );
}

void shader_destroy( Shader *shader ) {
    glDeleteProgram( shader->program );
    shader->program = 0;
}
