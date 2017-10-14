#ifndef HEADER_SHADERS_H
#define HEADER_SHADERS_H

uint shaders_compile( );
uint shaderCompileFromFile( int type, const char *filePath );
char *shaderLoadSource( const char *filePath );
void shaderAttachFromFile( GLuint program, GLenum type, const char *filePath );

#endif
