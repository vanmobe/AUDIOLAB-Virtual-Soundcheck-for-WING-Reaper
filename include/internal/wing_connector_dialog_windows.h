/*
 * Windows native WINGuard dialog header.
 */

#ifndef WING_CONNECTOR_DIALOG_WINDOWS_H
#define WING_CONNECTOR_DIALOG_WINDOWS_H

#include "internal/adoption_editor.h"

#ifdef _WIN32

extern "C" {
    void ShowWingConnectorDialogWindows();
    bool ShowExistingProjectAdoptionEditor(const std::vector<AdoptionEditorRow>& rows,
                                           const std::vector<int>& available_channels,
                                           const char* initial_output_mode,
                                           std::string& output_mode_out,
                                           std::string& channel_overrides_spec_out,
                                           std::string& slot_overrides_spec_out,
                                           bool& apply_now_out);
}

#endif

#endif
