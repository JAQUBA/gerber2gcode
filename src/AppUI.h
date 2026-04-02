#pragma once
// gerber2gcode — UI Layout & Creation

#include <UI/SimpleWindow/SimpleWindow.h>

// Layout constants
static const int TOOLBAR_HEIGHT  = 100;
static const int LAYER_PANEL_W   = 180;
static const int STATUS_BAR_H    = 24;
static const int LOG_AREA_H      = 120;

// UI creation
void createUI(SimpleWindow* window);

// Resize handler
void installResizeHandler();
void doResize(int w, int h);
