#pragma once

#if defined( REPGAME_GLES )
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#endif
#include "common/RepGameSDL2.hpp"

#define SUPPORTS_FRAME_BUFFER 1
#define REPGAME_PATH_DIVIDOR "/"

void repgame_linux_install_signal_handlers( );