#pragma once

#include <windows.h>

#include "imgui.h"
#include "device.h"

LRESULT WINAPI app_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
bool            app_surface_consume_resize(AppSurface& surface, unsigned& width, unsigned& height);
void            app_update_io_for_hwnd(HWND hwnd, ImGuiIO& io);
