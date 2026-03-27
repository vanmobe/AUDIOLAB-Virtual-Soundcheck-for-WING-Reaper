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

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace WingConnector {
namespace {

std::string TrimCopy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::vector<std::string> SplitDelimited(const std::string& value, char delimiter = ',') {
    std::vector<std::string> parts;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        parts.push_back(TrimCopy(item));
    }
    return parts;
}

const char* SourceKindToken(SourceKind kind) {
    switch (kind) {
        case SourceKind::Channel: return "CH";
        case SourceKind::Bus: return "BUS";
        case SourceKind::Main: return "MAIN";
        case SourceKind::Matrix: return "MTX";
    }
    return "CH";
}

int MaxSourceNumberForKind(SourceKind kind) {
    switch (kind) {
        case SourceKind::Channel: return 48;
        case SourceKind::Bus: return 16;
        case SourceKind::Main: return 4;
        case SourceKind::Matrix: return 8;
    }
    return 48;
}

bool ParseSourceKind(const std::string& token, SourceKind& kind_out) {
    std::string upper = token;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (upper == "CH" || upper == "CHANNEL") {
        kind_out = SourceKind::Channel;
        return true;
    }
    if (upper == "BUS" || upper == "BUS") {
        kind_out = SourceKind::Bus;
        return true;
    }
    if (upper == "MAIN") {
        kind_out = SourceKind::Main;
        return true;
    }
    if (upper == "MTX" || upper == "MATRIX") {
        kind_out = SourceKind::Matrix;
        return true;
    }
    return false;
}

std::string BuildMappingSpec(const std::vector<BridgeMapping>& mappings) {
    std::ostringstream out;
    bool first = true;
    for (const auto& mapping : mappings) {
        if (!mapping.enabled) {
            continue;
        }
        if (!first) {
            out << ";";
        }
        out << SourceKindToken(mapping.kind) << mapping.source_number << "=" << mapping.midi_value;
        first = false;
    }
    return out.str();
}

bool ParseMappingSpec(const std::string& spec, std::vector<BridgeMapping>& mappings_out, std::string& error_out) {
    mappings_out.clear();
    const std::string trimmed = TrimCopy(spec);
    if (trimmed.empty()) {
        return true;
    }

    for (const auto& token : SplitDelimited(trimmed, ';')) {
        if (token.empty()) {
            continue;
        }
        const size_t equals_pos = token.find('=');
        if (equals_pos == std::string::npos) {
            error_out = "Each mapping must look like CH1=10 or BUS3=25.";
            return false;
        }

        const std::string left = TrimCopy(token.substr(0, equals_pos));
        const std::string right = TrimCopy(token.substr(equals_pos + 1));
        size_t digit_pos = 0;
        while (digit_pos < left.size() && !std::isdigit(static_cast<unsigned char>(left[digit_pos]))) {
            ++digit_pos;
        }
        if (digit_pos == 0 || digit_pos >= left.size()) {
            error_out = "Mapping source must include a family and number, e.g. CH1.";
            return false;
        }

        SourceKind kind = SourceKind::Channel;
        if (!ParseSourceKind(left.substr(0, digit_pos), kind)) {
            error_out = "Unknown mapping family. Use CH, BUS, MAIN, or MTX.";
            return false;
        }

        int source_number = 0;
        int midi_value = 0;
        try {
            source_number = std::stoi(left.substr(digit_pos));
            midi_value = std::stoi(right);
        } catch (...) {
            error_out = "Mapping numbers must be integers.";
            return false;
        }

        if (source_number < 1 || source_number > MaxSourceNumberForKind(kind)) {
            error_out = "Mapping source number is out of range for its family.";
            return false;
        }
        if (midi_value < 0 || midi_value > 127) {
            error_out = "MIDI values must be between 0 and 127.";
            return false;
        }

        BridgeMapping mapping;
        mapping.kind = kind;
        mapping.source_number = source_number;
        mapping.midi_value = midi_value;
        mapping.enabled = true;
        mappings_out.push_back(mapping);
    }

    std::sort(mappings_out.begin(), mappings_out.end(), [](const BridgeMapping& lhs, const BridgeMapping& rhs) {
        if (lhs.kind != rhs.kind) {
            return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
        }
        return lhs.source_number < rhs.source_number;
    });
    return true;
}

std::string SelectedChannelBridgeSummary() {
    auto& extension = ReaperExtension::Instance();
    const std::string config_path = WingConfig::GetConfigPath();
    std::string selection_status = "Current selected strip: unavailable";

    if (!extension.IsConnected()) {
        selection_status =
            "No shared Wing connection is active yet. Open the main Wing action first and connect before enabling the bridge.";
    } else if (auto* osc = extension.GetOSCHandler()) {
        int strip_index = 0;
        if (osc->GetSelectedStripIndex(strip_index)) {
            selection_status = "Current selected strip id: " + std::to_string(strip_index) +
                               " (queried from /$ctl/$stat/selidx).";
        } else {
            selection_status =
                "Current selected strip query failed at /$ctl/$stat/selidx.";
        }
    }

    const auto& config = extension.GetConfig();
    std::ostringstream summary;
    summary
        << "SuperRack Bridge Summary\n\n"
        << selection_status << "\n"
        << "Bridge status: " << extension.GetBridgeStatusSummary() << "\n"
        << "Enabled: " << (config.bridge_enabled ? "yes" : "no") << "\n"
        << "MIDI output index: " << config.bridge_midi_output_device << "\n"
        << "MIDI behavior: " << config.bridge_midi_message_type << "\n"
        << "MIDI channel: " << config.bridge_midi_channel << "\n"
        << "Mappings: " << (config.bridge_mappings.empty() ? std::string("(none)") : BuildMappingSpec(config.bridge_mappings)) << "\n\n"
        << "Config file path:\n" << config_path << "\n";
    return summary.str();
}

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

bool ShowBridgeSetupWizard() {
    auto& extension = ReaperExtension::Instance();
    auto& config = extension.GetConfig();

    const std::vector<std::string> outputs = extension.GetMidiOutputDevices();
    std::ostringstream output_help;
    output_help << "Available MIDI outputs:\n";
    if (outputs.empty()) {
        output_help << "- none detected\n";
    } else {
        for (size_t i = 0; i < outputs.size(); ++i) {
            output_help << i << ": " << outputs[i] << "\n";
        }
    }
    output_help << "\nMapping format: CH1=10;BUS1=20;MAIN1=30;MTX1=40";
    ShowMessageBox(output_help.str().c_str(), "Behringer Wing: SuperRack Bridge", 0);

    char values[4096];
    std::snprintf(values, sizeof(values), "%d,%s,%d,%s,%d,%d",
                  config.bridge_midi_output_device,
                  config.bridge_midi_message_type.c_str(),
                  config.bridge_midi_channel,
                  BuildMappingSpec(config.bridge_mappings).c_str(),
                  config.bridge_enabled ? 1 : 0,
                  config.bridge_mappings.empty() ? 60 : config.bridge_mappings.front().midi_value);

    if (!GetUserInputs("SuperRack Bridge Setup",
                       6,
                       "MIDI output index,Behavior (NOTE_ON/NOTE_ON_OFF/PROGRAM),MIDI channel (1-16),Mappings,Enable bridge (0/1),Test MIDI value",
                       values,
                       sizeof(values))) {
        return false;
    }

    const auto parts = SplitDelimited(values, ',');
    if (parts.size() != 6) {
        ShowMessageBox("Expected 6 values from the bridge setup dialog.",
                       "Behringer Wing: SuperRack Bridge",
                       0);
        return false;
    }

    int output_index = -1;
    int midi_channel = 1;
    int enable_flag = 0;
    int test_value = 60;
    try {
        output_index = std::stoi(parts[0]);
        midi_channel = std::stoi(parts[2]);
        enable_flag = std::stoi(parts[4]);
        test_value = std::stoi(parts[5]);
    } catch (...) {
        ShowMessageBox("MIDI output, channel, enable flag, and test value must be integers.",
                       "Behringer Wing: SuperRack Bridge",
                       0);
        return false;
    }

    std::string behavior = parts[1];
    std::transform(behavior.begin(), behavior.end(), behavior.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (behavior != "NOTE_ON" && behavior != "NOTE_ON_OFF" && behavior != "PROGRAM") {
        ShowMessageBox("Behavior must be NOTE_ON, NOTE_ON_OFF, or PROGRAM.",
                       "Behringer Wing: SuperRack Bridge",
                       0);
        return false;
    }
    if (output_index < -1 || output_index >= static_cast<int>(outputs.size())) {
        ShowMessageBox("MIDI output index is out of range for the detected outputs.",
                       "Behringer Wing: SuperRack Bridge",
                       0);
        return false;
    }
    if (midi_channel < 1 || midi_channel > 16) {
        ShowMessageBox("MIDI channel must be between 1 and 16.",
                       "Behringer Wing: SuperRack Bridge",
                       0);
        return false;
    }
    if (test_value < 0 || test_value > 127) {
        ShowMessageBox("Test MIDI value must be between 0 and 127.",
                       "Behringer Wing: SuperRack Bridge",
                       0);
        return false;
    }

    std::vector<BridgeMapping> parsed_mappings;
    std::string mapping_error;
    if (!ParseMappingSpec(parts[3], parsed_mappings, mapping_error)) {
        ShowMessageBox(mapping_error.c_str(), "Behringer Wing: SuperRack Bridge", 0);
        return false;
    }

    config.bridge_midi_output_device = output_index;
    config.bridge_midi_message_type = behavior;
    config.bridge_midi_channel = midi_channel;
    config.bridge_mappings = std::move(parsed_mappings);
    config.bridge_enabled = (enable_flag != 0);

    const std::string config_path = WingConfig::GetConfigPath();
    if (!config.SaveToFile(config_path)) {
        ShowMessageBox("Failed to save bridge settings to config.json.",
                       "Behringer Wing: SuperRack Bridge",
                       0);
        return false;
    }

    extension.ApplyBridgeSettings();

    std::string test_detail;
    extension.SendBridgeTestMessage(test_value, test_detail);

    std::ostringstream result;
    result << SelectedChannelBridgeSummary() << "\n" << test_detail;
    ShowMessageBox(result.str().c_str(), "Behringer Wing: SuperRack Bridge", 0);
    return true;
}

} // namespace

void ShowMainDialog() {
#ifdef __APPLE__
    ShowWingConnectorDialogAtTab("setup");
#else
    RunCrossPlatformDialog();
#endif
}

void ShowSelectedChannelBridgeDialog() {
    ShowBridgeSetupWizard();
}

} // namespace WingConnector
