#pragma once


#include <GLES3/gl31.h>
#include <malloc.h>
#include <stdlib.h>
#include <android/log.h>

char *repgame_android_getShaderString( const char *filename );

#define SUPPORTS_FRAME_BUFFER 0
#define REPGAME_PATH_DIVIDOR "/"