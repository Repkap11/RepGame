#ifndef HEADER_SHADER_H
#define HEADER_SHADER_H

typedef struct {
    unsigned int program; //
} Shader;

void shader_init( Shader *shader );
void shader_bind( const Shader *shader );
void shader_unbind( const Shader *shader );
void shader_destroy( Shader *shader );

#endif