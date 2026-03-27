/*
 * Dialog Bridge
 * Provides cross-platform entrypoints for the Wing Connector UI.
 */

#ifndef DIALOG_BRIDGE_H
#define DIALOG_BRIDGE_H

namespace WingConnector {

// Opens the main Wing Connector UI flow for the current platform.
void ShowMainDialog();
// Opens the selected-channel bridge setup/help flow for the current platform.
void ShowSelectedChannelBridgeDialog();

} // namespace WingConnector

#endif // DIALOG_BRIDGE_H
