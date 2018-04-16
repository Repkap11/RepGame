#ifndef HEADER_SHADERS_H
#define HEADER_SHADERS_H

#include <GL/glew.h>

unsigned int shaders_compile( const char *vertex_path, const char *fragment_path );
unsigned int shaderCompileFromFile( int type, const char *filePath );
char *shaderLoadSource( const char *filePath );
void shaderAttachFromFile( GLuint program, GLenum type, const char *filePath );

#endif
