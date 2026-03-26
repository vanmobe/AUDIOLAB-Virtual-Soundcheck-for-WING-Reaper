/*
 * Cross-platform dialog bridge.
 */

#include <cstring>

#include "internal/dialog_bridge.h"
#include "wingconnector/reaper_extension.h"
#ifdef __APPLE__
#include "internal/wing_connector_dialog_macos.h"
#endif
#include "reaper_plugin_functions.h"

#include <string>

namespace WingConnector {
namespace {

#ifndef __APPLE__
void RunCrossPlatformDialog() {
    auto& extension = ReaperExtension::Instance();
    // Non-macOS fallback path: run a minimal "connect + configure all channels"
    // flow via standard REAPER dialogs.
    if (!extension.IsConnected()) {
        const bool connected = extension.ConnectToWing();
        if (!connected) {
            ShowMessageBox(
                "AUDIOLAB.wing.reaper.virtualsoundcheck could not connect.\n\n"
                "Set wing_ip in config.json and ensure OSC is enabled on the console.",
                "AUDIOLAB.wing.reaper.virtualsoundcheck",
                0);
            return;
        }
    }

    auto sources = extension.GetAvailableSources();
    if (sources.empty()) {
        ShowMessageBox(
            "Connected, but no selectable sources were discovered.",
            "AUDIOLAB.wing.reaper.virtualsoundcheck",
            0);
        return;
    }

    bool has_non_channel = false;
    for (auto& source : sources) {
        source.selected = true;
        has_non_channel = has_non_channel || !source.soundcheck_capable;
    }

    extension.SetupSoundcheckFromSelection(sources, true);
    ShowMessageBox(
        has_non_channel
            ? "Connected and configured live recording for available sources.\nSoundcheck routing was skipped for record-only buses/matrices.\nUse config.json for advanced selection and behavior."
            : "Connected and configured live recording for available channels.\nUse config.json for advanced selection and behavior.",
        "AUDIOLAB.wing.reaper.virtualsoundcheck",
        0);
}
#endif

} // namespace

void ShowMainDialog() {
#ifdef __APPLE__
    ShowWingConnectorDialog();
#else
    RunCrossPlatformDialog();
#endif
}

} // namespace WingConnector
