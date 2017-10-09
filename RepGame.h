#ifndef HEADER_TEST_H
#define HEADER_TEST_H

#include <stdio.h>

#define DEBUG 1

#define pr_debug(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s():"fmt"\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

#define test pr_debug();

#include "input.h"

typedef struct {
    int colorR;
    int colorG;
    int colorB;
    double frameRate;
    InputState input;
    struct {
        float angle;
        float lx;
        float lz;
        float x;
        float z;
    } camera;
    struct {
        float width;
        float height;
    } screen;

} RepGameState;

#endif