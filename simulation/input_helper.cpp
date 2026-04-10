#include "input_helper.h"
#include <windows.h>

bool checkInput(char &c) {
  HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD count;
  if (GetNumberOfConsoleInputEvents(hStdIn, &count) && count > 0) {
    INPUT_RECORD ir;
    DWORD read;
    // Peek first to see if it's a key down (ignoring other events)
    PeekConsoleInput(hStdIn, &ir, 1, &read);
    if (read > 0) {
      if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
        // It is a key press, read it to consume
        ReadConsoleInput(hStdIn, &ir, 1, &read);
        c = ir.Event.KeyEvent.uChar.AsciiChar;
        return c != 0; // Return true if valid char
      } else {
        // Consume non-key events (like key up or focus events)
        ReadConsoleInput(hStdIn, &ir, 1, &read);
      }
    }
  }
  return false;
}
