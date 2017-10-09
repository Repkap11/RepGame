#ifndef HEADER_INPUT_H
#define HEADER_INPUT_H

typedef struct {
    struct {
        int left;
        int right;
        int up;
        int down;
    } arrows;
    struct {
        struct {
            int left;
            int right;
            int middle;
        } buttons;
        struct {
            int x;
            int y;
        } currentPosition;
        struct {
            int x;
            int y;
        } rightPressedPosition;
    } mouse;
    int exitGame;
} InputState;

void input_arrowKeyDownInput(InputState* inputState, int key, int x, int y);
void input_arrowKeyUpInput(InputState* inputState, int key, int x, int y);
void input_mouseInput(InputState* inputState, int button, int state, int x, int y);
void input_keysInput(InputState* inputState, unsigned char key, int x, int y);
void input_mouseMove(InputState* inputState, int x, int y);

#endif