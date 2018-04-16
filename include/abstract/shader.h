#ifndef HEADER_SHADER_H
#define HEADER_SHADER_H

typedef struct {
    unsigned int m_RendererId; //
} Shader;

void shader_init( Shader *shader );
void shader_bind( const Shader *shader );
void shader_unbind( const Shader *shader );
void shader_destroy( Shader *shader );
void shader_set_uniform4f( Shader *shader, const char *name, float f0, float f1, float f2, float f3 );
#endif