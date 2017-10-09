#ifndef HEADER_TEST_H
#define HEADER_TEST_H

#include <stdio.h>

#define DEBUG 1

#define pr_debug(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s():"fmt"\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

#define test pr_debug();

typedef struct {
    int exitGame;
    int colorR;
    int colorG;
    int colorB;
    struct  {
        struct {
            int left;
            int right;
            int up;
            int down;
        } arrows;
        struct {
            int left;
            int right;
            int middle;
        } mouse;
    } input;

} RepGameState;

#endif