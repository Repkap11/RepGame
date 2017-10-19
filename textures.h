#ifndef HEADER_TEXTURES_H
#define HEADER_TEXTURES_H

#include "RepGame.h"
#include <GL/gl.h>

void textures_populate( );
GLuint textures_getDirtTexture( );
GLuint textures_getGrassTexture( );
GLuint textures_getSkyTexture( );
GLuint textures_getGrassSideTexture( );
GLuint textures_getWaterTexture( );
void textures_free( );
#endif
