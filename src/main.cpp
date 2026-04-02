// gerber2gcode — CNC PCB Isolation Router
// Converts KiCad Gerber/drill files to GCode for V-bit engraving and drilling.

#include <Core.h>
#include "AppState.h"
#include "AppUI.h"

#include <Util/ConfigManager.h>
#include <windows.h>

// ════════════════════════════════════════════════════════════════════════════
// Application lifecycle
// ════════════════════════════════════════════════════════════════════════════

void init() {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
}

void setup() {
    g_window = new SimpleWindow(1200, 760, "gerber2gcode", 0);
    g_window->init();
    g_window->setBackgroundColor(RGB(34, 37, 46));
    g_window->setTextColor(RGB(226, 230, 239));

    g_settings   = new ConfigManager("gerber2gcode.ini");
    g_toolConfig = new ConfigManager("tools.ini");

    // Build UI (toolbar, canvas, layer panel, log)
    createUI(g_window);

    // Load saved settings and tool presets
    loadSettings();

    // Welcome message
    logMsg("gerber2gcode - CNC PCB Isolation Router");
    logMsg("1. Click 'Open KiCad' or enter directory path");
    logMsg("2. Select tool preset (auto-configures all job options)");
    logMsg("3. Set laminate thickness only (Mat)");
    logMsg("4. Click Generate");
    logMsg("");

    // Auto-load project if saved KiCad dir exists
    if (g_fldKicadDir) {
        std::string dir = g_fldKicadDir->getText();
        if (!dir.empty()) {
            doLoadKicadDir();
        }
    }

    // onClose: save settings
    g_window->onClose([]() {
        saveSettings();
    });
}

void loop() {
    // Event-driven — nothing to do
}
