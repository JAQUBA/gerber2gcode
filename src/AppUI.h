#pragma once
// gerber2gcode — UI Layout & Creation

#include <UI/SimpleWindow/SimpleWindow.h>

// Layout constants
static const int TOOLBAR_HEIGHT  = 128;
static const int LAYER_PANEL_W   = 240;
static const int STATUS_BAR_H    = 20;
static const int LOG_AREA_H      = 128;

// UI creation
void createUI(SimpleWindow* window);

// Resize handler
void installResizeHandler();
void doResize(int w, int h);
