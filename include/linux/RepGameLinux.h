#ifndef HEADER_REPGAMELINUX_H
#define HEADER_REPGAMELINUX_H

#include <GL/glew.h>
#include <GL/freeglut.h>
#include <stdio.h>

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

#endif