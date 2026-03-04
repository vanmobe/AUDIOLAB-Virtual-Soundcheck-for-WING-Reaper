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
                "Wing Connector could not connect.\n\n"
                "Set wing_ip in config.json and ensure OSC is enabled on the console.",
                "Wing Connector",
                0);
            return;
        }
    }

    auto channels = extension.GetAvailableChannels();
    if (channels.empty()) {
        ShowMessageBox(
            "Connected, but no channels with sources were discovered.",
            "Wing Connector",
            0);
        return;
    }

    for (auto& channel : channels) {
        channel.selected = true;
    }

    extension.SetupSoundcheckFromSelection(channels, true);
    ShowMessageBox(
        "Connected and configured live recording for available channels.\n"
        "Use config.json for advanced selection and behavior.",
        "Wing Connector",
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
