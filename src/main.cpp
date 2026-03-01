/*
 * Wing Connector - Reaper Extension Entry Point
 * Integrates Behringer Wing console with Reaper via OSC
 */

#define REAPERAPI_IMPLEMENT
#include "reaper_plugin_functions.h"
#include "reaper_extension.h"
#ifdef __APPLE__
#include "wing_connector_dialog_macos.h"
#endif
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>

// Accelerator flags (from Windows API)
#define FVIRTKEY  0x01
#define FCONTROL  0x08
#define FSHIFT    0x04
#define FALT      0x10

using namespace WingConnector;

// ===== GLOBALS =====
REAPER_PLUGIN_HINSTANCE g_hInst = nullptr;
HWND g_hwndParent = nullptr;
static reaper_plugin_info_t* g_rec = nullptr;

// Command IDs
static int g_cmd_main_dialog = 0;

// ===== COMMAND HOOK (hookcommand2) =====
static bool OnAction(KbdSectionInfo* sec, int cmd, int val, int valhw, int relmode, HWND hwnd) {
    (void)sec;
    (void)val;
    (void)valhw;
    (void)relmode;
    (void)hwnd;

    fprintf(stderr, "🔧 [WING] OnAction() called with cmd=%d, g_cmd_main_dialog=%d\n", cmd, g_cmd_main_dialog);
    fflush(stderr);

    if (cmd == g_cmd_main_dialog) {
        fprintf(stderr, "🔧 [WING] Main dialog command triggered!\n");
        fflush(stderr);
        
        #ifdef __APPLE__
        fprintf(stderr, "🔧 [WING] Calling ShowWingConnectorDialog()\n");
        fflush(stderr);
        
        ShowWingConnectorDialog();
        
        fprintf(stderr, "🔧 [WING] ShowWingConnectorDialog() returned\n");
        fflush(stderr);
        #endif
        return true;
    }

    return false;
}

// ===== REGISTRATION =====
static void RegisterCommands() {
    fprintf(stderr, "🔧 [WING] RegisterCommands() called\n");
    fflush(stderr);
    
    if (!g_rec) {
        fprintf(stderr, "🔧 [WING] ERROR: g_rec is null\n");
        fflush(stderr);
        return;
    }
    
    fprintf(stderr, "🔧 [WING] Registering hook command\n");
    fflush(stderr);
    
    // Register the command hook
    g_rec->Register("hookcommand2", (void*)OnAction);
    
    fprintf(stderr, "🔧 [WING] Registering custom action\n");
    fflush(stderr);
    
    // Register actions
    custom_action_register_t action;
    memset(&action, 0, sizeof(action));
    
    // Main consolidated dialog
    action.uniqueSectionId = 0;
    action.idStr = "_WING_MAIN_DIALOG";
    action.name = "Wing: Connect to Behringer Wing";
    
    int ret = g_rec->Register("custom_action", &action);
    g_cmd_main_dialog = ret;
    
    fprintf(stderr, "🔧 [WING] Custom action registered with ID: %d\n", ret);
    fflush(stderr);
    
    // Register keyboard shortcut (this should NOT create a duplicate action)
    if (g_cmd_main_dialog > 0) {
        fprintf(stderr, "🔧 [WING] Registering keyboard shortcut Ctrl+Shift+W\n");
        fflush(stderr);
        gaccel_register_t accel;
        accel.accel.cmd = g_cmd_main_dialog;
        accel.accel.key = 'W';
        accel.accel.fVirt = FVIRTKEY | FCONTROL | FSHIFT;
        accel.desc = "";  // Empty desc - don't show in actions list
        g_rec->Register("gaccel", &accel);
    }
    
    fprintf(stderr, "🔧 [WING] RegisterCommands() complete\n");
    fflush(stderr);
}

// ===== ENTRY POINT =====
extern "C" {

int REAPER_PLUGIN_DLL_EXPORT REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE hInstance,
    reaper_plugin_info_t* rec)
{
    fprintf(stderr, "\n🔧 [WING] REAPER_PLUGIN_ENTRYPOINT called\n");
    fflush(stderr);
    
    if (!rec) {
        // Unloading
        fprintf(stderr, "🔧 [WING] Plugin unloading\n");
        fflush(stderr);
        ReaperExtension::Instance().Shutdown();
        return 0;
    }
    
    fprintf(stderr, "🔧 [WING] Plugin loading - setting up API\n");
    fflush(stderr);
    
    g_hInst = hInstance;
    g_rec = rec;
    g_hwndParent = rec->hwnd_main;
    
    // Load Reaper API
    REAPERAPI_LoadAPI(rec->GetFunc);
    
    // Check that GetFunc is available
    if (!rec->GetFunc) {
        fprintf(stderr, "🔧 [WING] ERROR: GetFunc not available\n");
        fflush(stderr);
        return 0;
    }
    
    fprintf(stderr, "🔧 [WING] Initializing ReaperExtension\n");
    fflush(stderr);
    
    // Initialize our extension
    if (!ReaperExtension::Instance().Initialize()) {
        fprintf(stderr, "🔧 [WING] ERROR: ReaperExtension::Initialize() failed\n");
        fflush(stderr);
        return 0;
    }
    
    fprintf(stderr, "🔧 [WING] Registering commands\n");
    fflush(stderr);
    
    // Register commands and actions
    RegisterCommands();
    
    fprintf(stderr, "🔧 [WING] Plugin initialization complete!\n");
    fflush(stderr);
    
    return 1;
}

} // extern "C"
