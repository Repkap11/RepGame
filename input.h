#ifndef HEADER_INPUT_H
#define HEADER_INPUT_H

#include "test.h"

void input_arrowKeyDownInput(RepGameState* gameState, int key, int x, int y);
void input_arrowKeyUpInput(RepGameState* gameState, int key, int x, int y);
void input_mouseInput(RepGameState* gameState, int button, int state, int x, int y);

#endif