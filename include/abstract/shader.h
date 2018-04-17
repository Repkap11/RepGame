#ifndef HEADER_SHADER_H
#define HEADER_SHADER_H

#include <glm/glm.hpp>

typedef struct {
    unsigned int m_RendererId; //
} Shader;

void shader_init( Shader *shader );
void shader_bind( const Shader *shader );
void shader_unbind( const Shader *shader );
void shader_destroy( Shader *shader );
void shader_set_uniform4f( Shader *shader, const char *name, float f0, float f1, float f2, float f3 );
void shader_set_uniform1i( Shader *shader, const char *name, int i );
void shader_set_uniform_mat4f( Shader *shader, const char *name, glm::mat4& mat );
#endif