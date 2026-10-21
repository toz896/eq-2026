// gui.h - Interfaz entre el plugin y su ventana
#pragma once
#include "state.h"

constexpr int GUI_W = 700;   // un poco mas ancha: ahora hay 7 perillas
constexpr int GUI_H = 430;

// parentWnd = HWND que entrega el host en effEditOpen. Devuelve un puntero opaco (o nullptr).
void* guiOpen(SharedState* st, void* parentWnd);
void  guiClose(void* gui);
