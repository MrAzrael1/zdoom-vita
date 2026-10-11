#ifndef ZDOOM_VITA_INPUT_H
#define ZDOOM_VITA_INPUT_H
#ifdef ZD_VITA
void I_VitaOpenConsoleKeyboard();
bool I_VitaSystemKeyboardActive();
bool I_VitaTouchPointer(int &x, int &y);
void I_VitaDrawTouchCursor();
#endif
#endif
