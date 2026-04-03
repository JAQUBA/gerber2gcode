#pragma once
// gerber2gcode — UI Layout & Creation

#include <UI/SimpleWindow/SimpleWindow.h>

// Layout constants
static const int TOOLBAR_HEIGHT  = 80;
static const int LAYER_PANEL_W   = 290;
static const int STATUS_BAR_H    = 20;
static const int LOG_AREA_H      = 112;

// UI creation
void createUI(SimpleWindow* window);

// Resize handler
void installResizeHandler();
void doResize(int w, int h);
