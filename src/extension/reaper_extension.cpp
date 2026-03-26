/*
 * Reaper Extension Main Class Implementation
 */

#include <cstring>

#include "wingconnector/reaper_extension.h"
#include "reaper_plugin_functions.h"
#ifdef __APPLE__
#include "internal/settings_dialog_macos.h"
#endif
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <ctime>
#include <cmath>
#include <limits>
#ifdef _WIN32
#include <sys/utime.h>
// windows.h defines min/max macros that break std::min/std::max usage.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#else
#include <utime.h>
#endif
#include "osc/OscOutboundPacketStream.h"
#include "ip/UdpSocket.h"

// C-style wrapper function for MIDI hook (REAPER requires a C function, not a member function)
extern "C" bool WingMidiInputHookWrapper(bool is_midi, const unsigned char* data, int len, int dev_id) {
    return WingConnector::ReaperExtension::MidiInputHook(is_midi, data, len, dev_id);
}

namespace WingConnector {

namespace {
constexpr int kChannelQueryAttempts = 2;
constexpr int kQueryResponseWaitMs = 250;  // Wait time for OSC responses after sending all queries
constexpr int kReaperPlayStatePlayingBit = 1;
constexpr int kReaperPlayStateRecordingBit = 4;
constexpr const char* kTrackSourceIdExtKey = "P_EXT:WINGCONNECTOR_SOURCE_ID";

const char* SourceKindTag(SourceKind kind) {
    switch (kind) {
        case SourceKind::Channel: return "CH";
        case SourceKind::Bus: return "BUS";
        case SourceKind::Matrix: return "MTX";
    }
    return "SRC";
}

std::string SourceIdentity(const SourceSelectionInfo& source) {
    return std::string(SourceKindTag(source.kind)) + std::to_string(source.source_number);
}

std::string SourceIdentity(SourceKind kind, int source_number) {
    return std::string(SourceKindTag(kind)) + std::to_string(source_number);
}

std::string SourcePersistentId(SourceKind kind, int source_number) {
    return std::string(SourceKindTag(kind)) + ":" + std::to_string(source_number);
}

std::string SourcePersistentId(const SourceSelectionInfo& source) {
    return SourcePersistentId(source.kind, source.source_number);
}

std::string NormalizeManagedTrackName(std::string track_name, const std::string& track_prefix) {
    const std::string prefixed = track_prefix.empty() ? std::string() : (track_prefix + " ");
    if (!prefixed.empty() && track_name.rfind(prefixed, 0) == 0) {
        track_name.erase(0, prefixed.size());
    }
    const std::string stereo_suffix = " (Stereo)";
    if (track_name.size() > stereo_suffix.size() &&
        track_name.compare(track_name.size() - stereo_suffix.size(), stereo_suffix.size(), stereo_suffix) == 0) {
        track_name.erase(track_name.size() - stereo_suffix.size());
    }
    return track_name;
}

long long SteadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void SendOscMessage(const std::string& host, int port, const std::string& address, int value = 1) {
    if (host.empty() || port <= 0 || address.empty()) {
        return;
    }
    try {
        char buffer[256];
        osc::OutboundPacketStream p(buffer, 256);
        p << osc::BeginMessage(address.c_str()) << (int32_t)value << osc::EndMessage;
        UdpTransmitSocket tx(IpEndpointName(host.c_str(), static_cast<uint16_t>(port)));
        tx.Send(p.Data(), p.Size());
    } catch (...) {
        // Never let OSC notification failures break transport control flow.
    }
}

void SendOscToWing(const WingConfig& cfg, const std::string& address, int value = 1) {
    SendOscMessage(cfg.wing_ip, 2223, address, value);
}

std::string RecorderTargetKey(const WingConfig& cfg) {
    return cfg.recorder_target == "USBREC" ? "USBREC" : "WLIVE";
}

const char* RecorderTargetLabel(const WingConfig& cfg) {
    return RecorderTargetKey(cfg) == "USBREC" ? "USB recorder" : "SD card (WING-LIVE)";
}

bool IsRecorderActiveState(const std::string& state) {
    return state == "REC" || state == "RECORD" || state == "RECORDING" || state == "PLAYREC";
}

std::string DescribeUSBRecorderStatus(WingOSC* osc_handler) {
    if (!osc_handler) {
        return "USB recorder status unavailable";
    }
    std::string active_state;
    std::string action_state;
    if (!osc_handler->GetUSBRecorderStatus(active_state, action_state)) {
        return "USB recorder status query failed";
    }
    return "USB recorder state=" + (active_state.empty() ? "?" : active_state) +
           ", action=" + (action_state.empty() ? "?" : action_state);
}

std::string DescribeWLiveRecorderStatus(WingOSC* osc_handler) {
    if (!osc_handler) {
        return "WING-LIVE recorder status unavailable";
    }
    std::vector<std::string> slot_descriptions;
    for (int slot = 1; slot <= 2; ++slot) {
        std::string state;
        std::string media_state;
        std::string error_message;
        std::string error_code;
        if (!osc_handler->GetWLiveRecorderStatus(slot, state, media_state, error_message, error_code)) {
            slot_descriptions.push_back("slot" + std::to_string(slot) + "=query failed");
            continue;
        }
        std::string slot_desc = "slot" + std::to_string(slot) +
                                " state=" + (state.empty() ? "?" : state) +
                                ", media=" + (media_state.empty() ? "?" : media_state);
        if (!error_code.empty() && error_code != "0") {
            slot_desc += ", error=" + error_code;
        }
        if (!error_message.empty()) {
            slot_desc += " (" + error_message + ")";
        }
        slot_descriptions.push_back(slot_desc);
    }
    std::string combined;
    for (size_t i = 0; i < slot_descriptions.size(); ++i) {
        if (i > 0) {
            combined += "; ";
        }
        combined += slot_descriptions[i];
    }
    return "WING-LIVE " + combined;
}

bool PollRecorderStarted(const WingConfig& cfg, WingOSC* osc_handler, std::string& detail_out) {
    if (!osc_handler) {
        detail_out = "recorder status unavailable";
        return false;
    }
    for (int attempt = 0; attempt < 30; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (RecorderTargetKey(cfg) == "USBREC") {
            std::string active_state;
            std::string action_state;
            if (osc_handler->GetUSBRecorderStatus(active_state, action_state)) {
                detail_out = DescribeUSBRecorderStatus(osc_handler);
                if (IsRecorderActiveState(active_state)) {
                    return true;
                }
            }
        } else {
            for (int slot = 1; slot <= 2; ++slot) {
                std::string state;
                std::string media_state;
                std::string error_message;
                std::string error_code;
                if (!osc_handler->GetWLiveRecorderStatus(slot, state, media_state, error_message, error_code)) {
                    continue;
                }
                detail_out = DescribeWLiveRecorderStatus(osc_handler);
                if (IsRecorderActiveState(state)) {
                    return true;
                }
            }
        }
    }
    if (detail_out.empty()) {
        detail_out = RecorderTargetKey(cfg) == "USBREC"
            ? DescribeUSBRecorderStatus(osc_handler)
            : DescribeWLiveRecorderStatus(osc_handler);
    }
    return false;
}

bool PollRecorderStopped(const WingConfig& cfg, WingOSC* osc_handler, std::string& detail_out) {
    if (!osc_handler) {
        detail_out = "recorder status unavailable";
        return false;
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (RecorderTargetKey(cfg) == "USBREC") {
            std::string active_state;
            std::string action_state;
            if (osc_handler->GetUSBRecorderStatus(active_state, action_state)) {
                detail_out = DescribeUSBRecorderStatus(osc_handler);
                if (!IsRecorderActiveState(active_state) && active_state == "STOP") {
                    return true;
                }
            }
        } else {
            bool all_stopped = true;
            bool any_query = false;
            for (int slot = 1; slot <= 2; ++slot) {
                std::string state;
                std::string media_state;
                std::string error_message;
                std::string error_code;
                if (!osc_handler->GetWLiveRecorderStatus(slot, state, media_state, error_message, error_code)) {
                    all_stopped = false;
                    continue;
                }
                any_query = true;
                if (IsRecorderActiveState(state)) {
                    all_stopped = false;
                }
            }
            detail_out = DescribeWLiveRecorderStatus(osc_handler);
            if (any_query && all_stopped) {
                return true;
            }
        }
    }
    if (detail_out.empty()) {
        detail_out = RecorderTargetKey(cfg) == "USBREC"
            ? DescribeUSBRecorderStatus(osc_handler)
            : DescribeWLiveRecorderStatus(osc_handler);
    }
    return false;
}

bool TouchFile(const std::string& path) {
    time_t now = time(nullptr);
#ifdef _WIN32
    struct _utimbuf times = {now, now};
    return _utime(path.c_str(), &times) == 0;
#else
    struct utimbuf times = {now, now};
    return utime(path.c_str(), &times) == 0;
#endif
}

std::vector<std::string> GetReaperKeymapPaths() {
    std::vector<std::string> paths;
    const char* resource_path = GetResourcePath();
    if (resource_path && *resource_path) {
        paths.emplace_back(std::string(resource_path) + "/reaper-kb.ini");
    }
#ifdef __APPLE__
    const char* home = std::getenv("HOME");
    if (home && *home) {
        paths.emplace_back(std::string(home) + "/Library/Application Support/REAPER/reaper-kb.ini");
    }
#elif defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) {
        paths.emplace_back(std::string(appdata) + "/REAPER/reaper-kb.ini");
    }
#else
    const char* home = std::getenv("HOME");
    if (home && *home) {
        paths.emplace_back(std::string(home) + "/.config/REAPER/reaper-kb.ini");
    }
#endif
    // Deduplicate while preserving order.
    std::vector<std::string> unique;
    for (const auto& p : paths) {
        if (std::find(unique.begin(), unique.end(), p) == unique.end()) {
            unique.push_back(p);
        }
    }
    return unique;
}
}  // namespace

// Static member definition
reaper_plugin_info_t* ReaperExtension::g_rec_ = nullptr;

// Helper function to parse channel selection (e.g., "1,3,5-7,10")
std::set<int> ParseChannelSelection(const std::string& selection_str, int max_channels) {
    std::set<int> selected;
    if (selection_str.empty()) return selected;
    std::istringstream iss(selection_str);
    std::string token;
    
    while (std::getline(iss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        
        if (token.empty()) continue;
        
        // Check if it's a range (e.g., "5-7")
        size_t dash_pos = token.find('-');
        if (dash_pos != std::string::npos && dash_pos > 0 && dash_pos < token.length() - 1) {
            try {
                int start = std::stoi(token.substr(0, dash_pos));
                int end = std::stoi(token.substr(dash_pos + 1));
                for (int i = start; i <= end && i <= max_channels; ++i) {
                    if (i > 0) selected.insert(i);
                }
            } catch (...) {
                // Skip invalid ranges
            }
        } else {
            // Single number
            try {
                int ch = std::stoi(token);
                if (ch > 0 && ch <= max_channels) {
                    selected.insert(ch);
                }
            } catch (...) {
                // Skip invalid numbers
            }
        }
    }
    
    return selected;
}

ReaperExtension::ReaperExtension()
    : connected_(false)
    , monitoring_enabled_(false)
    , soundcheck_mode_enabled_(false)
    , midi_actions_enabled_(false)
    , status_message_("Not connected")
    , log_callback_(nullptr)
{
}

ReaperExtension::~ReaperExtension() {
    Shutdown();
}

ReaperExtension& ReaperExtension::Instance() {
    static ReaperExtension instance;
    return instance;
}

void ReaperExtension::Log(const std::string& message) {
    if (log_callback_) {
        log_callback_(message);
    }
}

bool ReaperExtension::Initialize(reaper_plugin_info_t* rec) {
    // Store g_rec context for later use in EnableMidiActions
    if (rec) {
        g_rec_ = rec;
    }
    
    // Load configuration
    std::string config_path = WingConfig::GetConfigPath();
    
    bool loaded_user_config = config_.LoadFromFile(config_path);
    if (!loaded_user_config) {
        // Try loading from install directory
        if (!config_.LoadFromFile("config.json")) {
            Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Using default configuration\n");
        }
    } else {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Configuration loaded\n");
        
        bool config_updated = false;
        
        // Migrate legacy default listen port (2224 -> 2223)
        if (config_.listen_port == 2224) {
            config_.listen_port = 2223;
            config_updated = true;
            Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Updated listener port to 2223\n");
        }
        
        // Save updated config
        if (config_updated) {
            config_.SaveToFile(config_path);
        }
    }

    // Create track manager
    track_manager_ = std::make_unique<TrackManager>(config_);

    // Enable Wing MIDI device in REAPER settings
    EnableWingMidiDevice();

    // Enable MIDI actions if configured
    if (config_.configure_midi_actions) {
        EnableMidiActions(true);
    }

    if (g_rec_) {
        g_rec_->Register("timer", (void*)ReaperExtension::MainThreadTimerTick);
        // Register MIDI input hooks so CC actions work immediately without relying on kb.ini reload timing.
        g_rec_->Register("hook_midi_input", (void*)WingMidiInputHookWrapper);
        g_rec_->Register("hook_midiin", (void*)WingMidiInputHookWrapper);
    }
    
    return true;
}

void ReaperExtension::Shutdown() {
    if (g_rec_) {
        g_rec_->Register("-timer", (void*)ReaperExtension::MainThreadTimerTick);
        g_rec_->Register("-hook_midi_input", (void*)WingMidiInputHookWrapper);
        g_rec_->Register("-hook_midiin", (void*)WingMidiInputHookWrapper);
    }
    StopManualTransportFlash();
    StopMidiCapture();
    StopAutoRecordMonitor();
    EnableMidiActions(false);
    DisconnectFromWing();
    track_manager_.reset();
}

void ReaperExtension::MainThreadTimerTick() {
    auto& ext = ReaperExtension::Instance();
    constexpr int kCmdTransportRecord = 1013;
    constexpr int kCmdTransportStopSaveAllRecordedMedia = 40667;
    const long long now_ms = SteadyNowMs();

    auto stop_without_rewind = [&](double forced_restore_pos = std::numeric_limits<double>::quiet_NaN()) {
        const int state_before_stop = GetPlayState();
        const bool is_playing_before_stop = (state_before_stop & kReaperPlayStatePlayingBit) != 0;
        const bool is_recording_before_stop = (state_before_stop & kReaperPlayStateRecordingBit) != 0;
        if (!is_playing_before_stop && !is_recording_before_stop) {
            return;
        }
        ReaProject* proj = EnumProjects(-1, nullptr, 0);
        double restore_pos = proj ? GetPlayPositionEx(proj) : GetPlayPosition();
        if (!std::isnan(forced_restore_pos)) {
            restore_pos = forced_restore_pos;
        }
        Main_OnCommand(kCmdTransportStopSaveAllRecordedMedia, 0);
        if (proj) {
            SetEditCurPos2(proj, restore_pos, false, false);
        } else {
            SetEditCurPos(restore_pos, false, false);
        }
    };

    const long long guard_until = ext.transport_guard_until_ms_.load();
    if (guard_until > 0 && now_ms < guard_until && ext.transport_guard_from_stopped_state_.load()) {
        const int play_state = GetPlayState();
        if ((play_state & kReaperPlayStatePlayingBit) != 0 || (play_state & kReaperPlayStateRecordingBit) != 0) {
            stop_without_rewind(ext.transport_guard_restore_pos_.load());
            ext.StopManualTransportFlash();
        }
    } else if (guard_until > 0 && now_ms >= guard_until) {
        ext.transport_guard_until_ms_ = 0;
        ext.transport_guard_from_stopped_state_ = false;
    }

    // Hard guard: while assignment/sync suppression is active, drop queued actions.
    if (now_ms < ext.suppress_all_cc_until_ms_.load()) {
        ext.pending_midi_command_ = 0;
        ext.pending_record_start_ = false;
        ext.pending_record_stop_ = false;
        ext.pending_toggle_soundcheck_mode_ = false;
        return;
    }

    const int play_state_now = GetPlayState();
    const bool is_recording_now = (play_state_now & kReaperPlayStateRecordingBit) != 0;
    if (ext.pending_record_start_.exchange(false)) {
        // Record action is a toggle in REAPER; only issue it when not already recording.
        if (!is_recording_now) {
            ext.Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Auto-trigger requesting REAPER transport record.\n");
            Main_OnCommand(kCmdTransportRecord, 0);  // Transport: Record (main thread)
        } else {
            ext.Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Auto-trigger requested record, but REAPER was already recording.\n");
        }
    }
    if (ext.pending_record_stop_.exchange(false)) {
        stop_without_rewind();
    }
    if (ext.pending_toggle_soundcheck_mode_.exchange(false)) {
        ext.ToggleSoundcheckMode();
    }
    const int midi_cmd = ext.pending_midi_command_.exchange(0);
    if (midi_cmd > 0) {
        if (midi_cmd == kCmdTransportRecord) {
            // Record action is a toggle in REAPER; only issue it when not already recording.
            if (!is_recording_now) {
                Main_OnCommand(midi_cmd, 0);
            }
        } else if (midi_cmd == kCmdTransportStopSaveAllRecordedMedia) {
            stop_without_rewind();
        } else {
            Main_OnCommand(midi_cmd, 0);
        }
    }

    const int play_state_after_actions = GetPlayState();
    const bool is_recording_after_actions = (play_state_after_actions & kReaperPlayStateRecordingBit) != 0;
    if (is_recording_after_actions != is_recording_now) {
        ext.Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: REAPER transport changed to ") +
                (is_recording_after_actions ? "RECORDING.\n" : "STOPPED.\n"));
    }
    ext.SyncExternalRecorderWithReaperState(is_recording_after_actions);
}

// Connects and verifies OSC reachability only; track creation is user-driven.
bool ReaperExtension::ConnectToWing() {
    // Avoid unintended transport commands from transient Wing MIDI echoes
    // while connection/setup traffic is in flight.
    struct ScopedMidiSuppress {
        std::atomic<bool>& flag;
        explicit ScopedMidiSuppress(std::atomic<bool>& f) : flag(f) { flag = true; }
        ~ScopedMidiSuppress() { flag = false; }
    } midi_suppress_guard(suppress_midi_processing_);

    if (connected_) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Already connected\n");
        return true;
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Connecting to Wing...\n");
    status_message_ = "Connecting...";
    
    // Wing OSC is fixed to 2223.
    config_.wing_port = 2223;
    config_.listen_port = 2223;

    // Create OSC handler
    osc_handler_ = std::make_unique<WingOSC>(
        config_.wing_ip,
        config_.wing_port,
        config_.listen_port
    );
    
    // Set callback
    osc_handler_->SetChannelCallback(
        [this](const ChannelInfo& channel) {
            OnChannelDataReceived(channel);
        }
    );
    
    // Start OSC server
    if (!osc_handler_->Start()) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Failed to start OSC server. Port may be in use.\n");
        osc_handler_.reset();
        status_message_ = "Failed to start";
        return false;
    }
    
    // Test connection
    if (!osc_handler_->TestConnection()) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Could not connect to Wing console. Check IP and OSC settings.\n");
        osc_handler_->Stop();
        osc_handler_.reset();
        status_message_ = "Connection failed";
        return false;
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Connected!\n");
    connected_ = true;
    status_message_ = "Connected";
    StartAutoRecordMonitor();
    
    // Query console info
    const auto& wing_info = osc_handler_->GetWingInfo();
    if (!wing_info.model.empty()) {
        char info_msg[256];
        snprintf(info_msg, sizeof(info_msg),
                 "AUDIOLAB.wing.reaper.virtualsoundcheck: Detected %s (%s) FW %s\n",
                 wing_info.model.c_str(),
                 wing_info.name.empty() ? "Unnamed" : wing_info.name.c_str(),
                 wing_info.firmware.empty() ? "unknown" : wing_info.firmware.c_str());
        Log(info_msg);
    }

    if (config_.sd_lr_route_enabled) {
        ApplyRecorderRoutingNoDialog();
        Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: Requested Main LR routing to ") +
            RecorderTargetLabel(config_) + " 1/2 on connect (verify on WING).\n");
    }

    // If MIDI actions are enabled in the extension, re-apply current mapping to the Wing.
    SyncMidiActionsToWing();
    
    return true;
}

// Get available recording sources with routing assigned.
std::vector<SourceSelectionInfo> ReaperExtension::GetAvailableSources() {
    std::vector<SourceSelectionInfo> result;
    
    if (!connected_ || !osc_handler_) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Not connected. Cannot query sources.\n");
        return result;
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Querying channel sources...\n");

    std::set<std::string> existing_source_ids;
    std::set<std::string> existing_source_names;
    if (track_manager_) {
        const auto existing_tracks = track_manager_->FindExistingWingTracks();
        for (MediaTrack* track : existing_tracks) {
            if (!track) {
                continue;
            }

            char source_id_buf[256];
            if (GetSetMediaTrackInfo_String(track, kTrackSourceIdExtKey, source_id_buf, false) &&
                source_id_buf[0] != '\0') {
                existing_source_ids.insert(source_id_buf);
            }

            char name_buf[512];
            if (GetSetMediaTrackInfo_String(track, "P_NAME", name_buf, false) && name_buf[0] != '\0') {
                existing_source_names.insert(NormalizeManagedTrackName(name_buf, config_.track_prefix));
            }
        }
    }
    for (const auto& source_id : config_.last_selected_source_ids) {
        existing_source_ids.insert(source_id);
    }
    const bool has_existing_selection = !existing_source_ids.empty() || !existing_source_names.empty();
    
    // Query all channels with retries
    const auto query_delay = std::chrono::milliseconds(kQueryResponseWaitMs);
    for (int attempt = 1; attempt <= kChannelQueryAttempts; ++attempt) {
        char attempt_msg[128];
        snprintf(attempt_msg, sizeof(attempt_msg),
                 "AUDIOLAB.wing.reaper.virtualsoundcheck: Querying channels (attempt %d/%d)\n",
                 attempt, kChannelQueryAttempts);
        Log(attempt_msg);
        osc_handler_->QueryAllChannels(config_.channel_count);
        std::this_thread::sleep_for(query_delay);
        if (!osc_handler_->GetChannelData().empty()) {
            break;
        }
        if (attempt < kChannelQueryAttempts) {
            Log("AUDIOLAB.wing.reaper.virtualsoundcheck: No channel data yet, retrying...\n");
        }
    }
    
    // Get channel data
    const auto& channel_data = osc_handler_->GetChannelData();
    if (channel_data.empty()) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: No channel data received. Check timeout settings.\n");
        return result;
    }

    // Query USR routing so popup can display resolved sources (e.g. USR:25 -> A:8)
    osc_handler_->QueryUserSignalInputs(48);

    auto use_direct_display_source = [](const std::string& grp) {
        return grp == "A" || grp == "LCL" || grp == "USR";
    };

    // Query source labels used by the selection popup (for name fallback).
    std::set<std::pair<std::string, int>> source_endpoints;
    std::map<int, std::pair<std::string, int>> channel_display_sources;
    for (const auto& pair : channel_data) {
        const ChannelInfo& ch = pair.second;
        if (ch.primary_source_group.empty() || ch.primary_source_input <= 0) {
            continue;
        }
        std::pair<std::string, int> display_source =
            use_direct_display_source(ch.primary_source_group)
                ? std::make_pair(ch.primary_source_group, ch.primary_source_input)
                : osc_handler_->ResolveRoutingChain(ch.primary_source_group, ch.primary_source_input);
        source_endpoints.insert(display_source);
        channel_display_sources[ch.channel_number] = display_source;
        if (ch.stereo_linked) {
            source_endpoints.insert({display_source.first, display_source.second + 1});
        }
    }
    osc_handler_->QueryInputSourceNames(source_endpoints);

    std::set<std::pair<std::string, int>> missing_channel_name_sources;
    for (const auto& pair : channel_data) {
        const ChannelInfo& ch = pair.second;
        const bool has_generic_channel_name =
            ch.name.empty() || ch.name.rfind("CH", 0) == 0 || ch.name.rfind("Channel ", 0) == 0;
        if (!has_generic_channel_name) {
            continue;
        }
        auto it = channel_display_sources.find(ch.channel_number);
        if (it == channel_display_sources.end()) {
            continue;
        }
        const auto& [grp, in] = it->second;
        if (!osc_handler_->GetInputSourceName(grp, in).empty()) {
            continue;
        }
        missing_channel_name_sources.insert({grp, in});
    }
    std::map<std::string, std::string> direct_channel_names;
    if (!missing_channel_name_sources.empty()) {
        std::vector<std::string> missing_name_addresses;
        missing_name_addresses.reserve(missing_channel_name_sources.size());
        for (const auto& [grp, in] : missing_channel_name_sources) {
            missing_name_addresses.push_back("/io/in/" + grp + "/" + std::to_string(in) + "/name");
        }
        direct_channel_names = osc_handler_->QueryStringAddressesDirect(missing_name_addresses, 100, 15);
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Processing channel data...\n");
    // stereo_linked is set from /io/in/{grp}/{num}/mode by the second pass in QueryAllChannels.
    // No heuristics needed.
    
    // Build list of channels with sources.
    // On Behringer Wing: stereo is SOURCE-based, not channel-based.
    // A channel is stereo if its source is stereo. That's it - no pairing needed.
    
    for (const auto& pair : channel_data) {
        const ChannelInfo& ch = pair.second;
        
        if (ch.primary_source_group.empty()) {
            continue;
        }

        SourceSelectionInfo info;
        info.kind = SourceKind::Channel;
        info.source_number = ch.channel_number;
        info.name = ch.name;
        info.source_group = ch.primary_source_group;
        info.source_input = ch.primary_source_input;
        
        // stereo if source mode was "ST" or "MS" (set by QueryChannelSourceStereo)
        bool is_stereo = ch.stereo_linked;
        
        const std::pair<std::string, int> raw_source = {ch.primary_source_group, ch.primary_source_input};
        const std::pair<std::string, int> display_source =
            use_direct_display_source(raw_source.first)
                ? raw_source
                : osc_handler_->ResolveRoutingChain(raw_source.first, raw_source.second);
        info.source_group = display_source.first;
        info.source_input = display_source.second;

        // For stereo sources, the partner is always source_input+1 in the same group
        if (is_stereo) {
            info.partner_source_group = info.source_group;
            info.partner_source_input = info.source_input + 1;
        }

        const bool has_generic_channel_name =
            info.name.empty() || info.name.rfind("CH", 0) == 0 || info.name.rfind("Channel ", 0) == 0;
        if (has_generic_channel_name) {
            std::string src_name = osc_handler_->GetInputSourceName(info.source_group, info.source_input);
            if (src_name.empty()) {
                const std::string key = "/io/in/" + info.source_group + "/" + std::to_string(info.source_input) + "/name";
                auto direct_it = direct_channel_names.find(key);
                if (direct_it != direct_channel_names.end()) {
                    src_name = direct_it->second;
                }
            }
            if (!src_name.empty()) {
                info.name = src_name;
            }
        }

        info.stereo_linked = is_stereo;
        info.selected = has_existing_selection
            ? (existing_source_ids.count(SourcePersistentId(info)) > 0 ||
               existing_source_names.count(info.name) > 0)
            : false;
        info.soundcheck_capable = true;

        result.push_back(info);
    }

    auto append_record_only_sources = [&](SourceKind kind,
                                          const std::string& route_group,
                                          int count,
                                          const std::string& fallback_prefix,
                                          const std::map<int, std::string>& modes,
                                          const std::map<int, std::string>& names) {
        for (int i = 1; i <= count; ++i) {
            auto mode_it = modes.find(i);
            std::string mode = (mode_it != modes.end()) ? mode_it->second : "";
            if (mode.empty()) {
                continue;
            }
            const bool is_stereo = (mode == "ST" || mode == "MS");
            if (is_stereo && (i % 2 == 0)) {
                continue;
            }

            SourceSelectionInfo info;
            info.kind = kind;
            info.source_number = i;
            info.source_group = route_group;
            info.source_input = i;
            info.stereo_linked = is_stereo;
            info.soundcheck_capable = false;
            info.selected = has_existing_selection
                ? (existing_source_ids.count(SourcePersistentId(info)) > 0 ||
                   existing_source_names.count(info.name) > 0)
                : false;

            auto name_it = names.find(i);
            std::string name = (name_it != names.end()) ? name_it->second : "";
            if (name.empty()) {
                name = info.stereo_linked
                    ? (fallback_prefix + " " + std::to_string(i) + "-" + std::to_string(i + 1))
                    : (fallback_prefix + " " + std::to_string(i));
            }
            info.name = name;

            if (info.stereo_linked) {
                info.partner_source_group = route_group;
                info.partner_source_input = i + 1;
            }

            result.push_back(info);
        }
    };

    auto load_record_only_metadata = [&](const std::string& query_group,
                                         const std::string& console_prefix,
                                         int count) {
        std::vector<std::string> addresses;
        addresses.reserve(static_cast<size_t>(count) * 3);
        for (int i = 1; i <= count; ++i) {
            char formatted[8];
            snprintf(formatted, sizeof(formatted), "%02d", i);
            addresses.push_back("/io/in/" + query_group + "/" + std::to_string(i) + "/mode");
            addresses.push_back("/" + console_prefix + "/" + std::string(formatted) + "/name");
            addresses.push_back("/" + console_prefix + "/" + std::to_string(i) + "/name");
        }

        std::map<int, std::string> modes;
        std::map<int, std::string> names;
        const auto replies = osc_handler_->QueryStringAddressesDirect(addresses, 140, 20);
        for (int i = 1; i <= count; ++i) {
            char formatted[8];
            snprintf(formatted, sizeof(formatted), "%02d", i);
            const std::string mode_path = "/io/in/" + query_group + "/" + std::to_string(i) + "/mode";
            auto mode_it = replies.find(mode_path);
            if (mode_it != replies.end()) {
                modes[i] = mode_it->second;
            }

            const std::string name_path_a = "/" + console_prefix + "/" + std::string(formatted) + "/name";
            const std::string name_path_b = "/" + console_prefix + "/" + std::to_string(i) + "/name";
            auto name_it = replies.find(name_path_a);
            if (name_it != replies.end() && !name_it->second.empty()) {
                names[i] = name_it->second;
                continue;
            }
            name_it = replies.find(name_path_b);
            if (name_it != replies.end() && !name_it->second.empty()) {
                names[i] = name_it->second;
            }
        }
        return std::make_pair(std::move(modes), std::move(names));
    };

    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Querying bus and matrix sources...\n");
    auto [bus_modes, bus_names] = load_record_only_metadata("$BUS", "bus", 32);
    auto [mtx_modes, mtx_names] = load_record_only_metadata("$MTX", "mtx", 16);

    append_record_only_sources(SourceKind::Bus, "BUS", 32, "Bus", bus_modes, bus_names);
    append_record_only_sources(SourceKind::Matrix, "MTX", 16, "Matrix", mtx_modes, mtx_names);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "AUDIOLAB.wing.reaper.virtualsoundcheck: Found %d selectable sources\n", 
             (int)result.size());
    Log(msg);
    
    return result;
}

void ReaperExtension::CreateTracksFromSelection(const std::vector<SourceSelectionInfo>& channels) {
    if (!connected_ || !osc_handler_) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Not connected\n");
        return;
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Creating tracks from selection...\n");
    
    std::vector<SourceSelectionInfo> selected;
    for (const auto& ch : channels) {
        if (ch.selected) {
            selected.push_back(ch);
        }
    }
    
    if (selected.empty()) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: No sources selected\n");
        return;
    }

    auto allocations = osc_handler_->CalculateUSBAllocation(selected);
    int track_index = 0;
    int created_tracks = 0;
    const auto& channel_data = osc_handler_->GetChannelData();

    Undo_BeginBlock();
    for (const auto& alloc : allocations) {
        auto it = std::find_if(selected.begin(), selected.end(), [&](const SourceSelectionInfo& source) {
            return source.kind == alloc.source_kind && source.source_number == alloc.source_number;
        });
        if (it == selected.end()) {
            continue;
        }

        int color_id = 0;
        if (it->kind == SourceKind::Channel) {
            auto ch_it = channel_data.find(it->source_number);
            if (ch_it != channel_data.end()) {
                color_id = ch_it->second.color;
            }
        }

        MediaTrack* track = alloc.is_stereo
            ? track_manager_->CreateStereoTrack(track_index, it->name, color_id)
            : track_manager_->CreateTrack(track_index, it->name, color_id);
        if (!track) {
            continue;
        }

        track_manager_->SetTrackInput(track, alloc.usb_start - 1, alloc.is_stereo ? 2 : 1);
        const std::string source_id = SourcePersistentId(*it);
        GetSetMediaTrackInfo_String(track, kTrackSourceIdExtKey, const_cast<char*>(source_id.c_str()), true);
        if (it->soundcheck_capable) {
            track_manager_->SetTrackHardwareOutput(track, alloc.usb_start - 1, alloc.is_stereo ? 2 : 1);
        } else {
            track_manager_->ClearTrackHardwareOutputs(track);
        }
        SetMediaTrackInfo_Value(track, "B_MAINSEND", 0);
        track_index++;
        created_tracks++;
    }
    Undo_EndBlock("AUDIOLAB.wing.reaper.virtualsoundcheck: Create tracks from selected sources", UNDO_STATE_TRACKCFG);

    char msg[128];
    snprintf(msg, sizeof(msg), "AUDIOLAB.wing.reaper.virtualsoundcheck: Created %d tracks\n", created_tracks);
    Log(msg);
}

bool ReaperExtension::CheckOutputModeAvailability(const std::string& output_mode, std::string& details) const {
    const std::string mode = (output_mode == "CARD") ? "CARD" : "USB";
    const int required_channels = (mode == "CARD") ? 32 : 48;

    const int available_inputs = GetNumAudioInputs();
    const int available_outputs = GetNumAudioOutputs();

    if (available_inputs < required_channels || available_outputs < required_channels) {
        details = "Selected mode " + mode + " may not be fully available in REAPER device I/O. "
                  "Required (full bank): " + std::to_string(required_channels) + " in / " +
                  std::to_string(required_channels) + " out, available: " +
                  std::to_string(available_inputs) + " in / " + std::to_string(available_outputs) + " out.";
        return false;
    }

    details = mode + " mode available in REAPER device I/O (" +
              std::to_string(available_inputs) + " in / " +
              std::to_string(available_outputs) + " out).";
    return true;
}

bool ReaperExtension::ValidateLiveRecordingSetup(std::string& details) {
    if (!connected_ || !osc_handler_) {
        details = "Not connected to Wing.";
        return false;
    }

    // Refresh channel/ALT state from the console before validating.
    osc_handler_->QueryAllChannels(config_.channel_count);
    std::this_thread::sleep_for(std::chrono::milliseconds(kQueryResponseWaitMs));

    const auto& channel_data = osc_handler_->GetChannelData();
    if (channel_data.empty()) {
        details = "No channel data received from Wing.";
        return false;
    }

    const bool card_mode = (config_.soundcheck_output_mode == "CARD");
    std::set<std::string> accepted_alt_groups;
    if (card_mode) {
        accepted_alt_groups.insert("CARD");
        accepted_alt_groups.insert("CRD");
    } else {
        accepted_alt_groups.insert("USB");
    }

    // Gather channels that are wired for live/soundcheck switching.
    std::set<int> expected_track_inputs_1based;
    int routable_channels = 0;
    int alt_configured_channels = 0;
    for (const auto& [ch_num, ch] : channel_data) {
        (void)ch_num;
        if (ch.primary_source_group.empty() || ch.primary_source_group == "OFF" || ch.primary_source_input <= 0) {
            continue;
        }
        routable_channels++;

        if (accepted_alt_groups.count(ch.alt_source_group) > 0 && ch.alt_source_input > 0) {
            alt_configured_channels++;
            expected_track_inputs_1based.insert(ch.alt_source_input);
        }
    }

    if (routable_channels == 0) {
        details = "Wing has no routable input channels.";
        return false;
    }

    if (expected_track_inputs_1based.empty()) {
        details = "Wing ALT sources are not configured for " + std::string(card_mode ? "CARD" : "USB") + ".";
        return false;
    }

    // Validate REAPER tracks against expected I/O mapping:
    // - Track record input should map to ALT input
    // - Track should have a matching hardware output send
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    if (!proj) {
        details = "No active REAPER project.";
        return false;
    }

    int matching_tracks = 0;
    std::set<int> matched_inputs_1based;
    const int track_count = CountTracks(proj);
    for (int i = 0; i < track_count; ++i) {
        MediaTrack* track = GetTrack(proj, i);
        if (!track) {
            continue;
        }

        int rec_input = (int)GetMediaTrackInfo_Value(track, "I_RECINPUT");
        if (rec_input < 0) {
            continue;
        }

        const int rec_input_index = rec_input & 0x3FF;  // 0-based device channel index
        const int rec_input_1based = rec_input_index + 1;
        if (expected_track_inputs_1based.count(rec_input_1based) == 0) {
            continue;
        }

        bool has_matching_hw_send = false;
        const int hw_send_count = GetTrackNumSends(track, 1);
        for (int s = 0; s < hw_send_count; ++s) {
            const int dst = (int)GetTrackSendInfo_Value(track, 1, s, "I_DSTCHAN");
            const int dst_index = dst & 0x3FF;  // mono/stereo encoded in high bits
            if (dst_index == rec_input_index) {
                has_matching_hw_send = true;
                break;
            }
        }

        if (!has_matching_hw_send) {
            continue;
        }

        matching_tracks++;
        matched_inputs_1based.insert(rec_input_1based);
    }

    if (matching_tracks == 0 || matched_inputs_1based.empty()) {
        details = "No REAPER tracks match Wing ALT input/hardware routing.";
        return false;
    }

    if (matched_inputs_1based.size() < expected_track_inputs_1based.size()) {
        std::ostringstream msg;
        msg << "Partial setup: matched " << matched_inputs_1based.size()
            << " of " << expected_track_inputs_1based.size()
            << " expected ALT-mapped input routes.";
        details = msg.str();
        return false;
    }

    std::ostringstream ok;
    ok << "Validated: " << alt_configured_channels << " Wing channels have ALT routing and "
       << matching_tracks << " REAPER tracks match live I/O routing.";
    details = ok.str();
    return true;
}

void ReaperExtension::SetupSoundcheckFromSelection(const std::vector<SourceSelectionInfo>& channels, bool setup_soundcheck, bool replace_existing) {
    // During live-setup operations, ignore incoming MIDI hook traffic from Wing.
    struct ScopedMidiSuppress {
        std::atomic<bool>& flag;
        explicit ScopedMidiSuppress(std::atomic<bool>& f) : flag(f) { flag = true; }
        ~ScopedMidiSuppress() { flag = false; }
    } midi_suppress_guard(suppress_midi_processing_);

    if (!connected_ || !osc_handler_) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Not connected\n");
        return;
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Setting up Virtual Soundcheck...\n");
    
    std::vector<SourceSelectionInfo> selected_sources;
    const auto& channel_data = osc_handler_->GetChannelData();
    
    for (const auto& sel : channels) {
        if (!sel.selected) {
            continue;
        }

        SourceSelectionInfo selected = sel;
        if (selected.kind == SourceKind::Channel) {
            auto it = channel_data.find(selected.source_number);
            if (it == channel_data.end()) {
                continue;
            }
            if (selected.name.empty()) {
                selected.name = it->second.name.empty()
                    ? ("CH" + std::to_string(selected.source_number))
                    : it->second.name;
            }
        }
        selected_sources.push_back(selected);
    }
    
    if (selected_sources.empty()) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: No sources selected\n");
        return;
    }
    
    Log("Refreshing selected channel source metadata from Wing...\n");
    for (const auto& source : selected_sources) {
        if (source.kind == SourceKind::Channel) {
            osc_handler_->QueryChannel(source.source_number);
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto& updated_channel_data = osc_handler_->GetChannelData();
    for (auto& selected : selected_sources) {
        if (selected.kind != SourceKind::Channel) {
            continue;
        }
        auto it = updated_channel_data.find(selected.source_number);
        if (it != updated_channel_data.end()) {
            selected.stereo_linked = it->second.stereo_linked;
            if (selected.name.empty()) {
                selected.name = it->second.name.empty()
                    ? ("CH" + std::to_string(selected.source_number))
                    : it->second.name;
            }
        }
    }
    
    auto allocations = osc_handler_->CalculateUSBAllocation(selected_sources);
    const bool has_soundcheck_capable = std::any_of(selected_sources.begin(), selected_sources.end(), [](const SourceSelectionInfo& source) {
        return source.soundcheck_capable;
    });
    const bool enable_soundcheck_setup = setup_soundcheck && has_soundcheck_capable;
    
    // Get output mode for display
    std::string output_mode = config_.soundcheck_output_mode;
    std::string output_type = (output_mode == "CARD") ? "CARD" : "USB";
    const int wing_bank_limit = (output_mode == "CARD") ? 32 : 48;

    int required_input_channels = 0;
    int required_output_channels = 0;
    for (const auto& alloc : allocations) {
        if (!alloc.allocation_note.empty() && alloc.allocation_note.find("ERROR") != std::string::npos) {
            continue;
        }
        if (alloc.usb_end > required_input_channels) {
            required_input_channels = alloc.usb_end;
        }
        if (!enable_soundcheck_setup) {
            continue;
        }
        auto it = std::find_if(selected_sources.begin(), selected_sources.end(), [&](const SourceSelectionInfo& source) {
            return source.kind == alloc.source_kind && source.source_number == alloc.source_number;
        });
        if (it != selected_sources.end() && it->soundcheck_capable && alloc.usb_end > required_output_channels) {
            required_output_channels = alloc.usb_end;
        }
    }

    if (required_input_channels > wing_bank_limit) {
        std::ostringstream err;
        err << "AUDIOLAB.wing.reaper.virtualsoundcheck: Selected sources exceed the Wing "
            << output_type << " bank. Required by current selection: " << required_input_channels
            << ", available on Wing: " << wing_bank_limit << ".\n";
        Log(err.str());

        std::ostringstream msg;
        msg << "Selected " << output_type << " routing needs " << required_input_channels
            << " channels, but the Wing exposes only " << wing_bank_limit
            << " " << output_type << " slots.\n\n"
            << "The last stereo pair would be incomplete or missing.\n\n"
            << "Choose fewer sources or switch output mode.";
        ShowMessageBox(msg.str().c_str(), "AUDIOLAB.wing.reaper.virtualsoundcheck - Output Bank Exceeded", 0);
        return;
    }

    const int available_inputs = GetNumAudioInputs();
    const int available_outputs = GetNumAudioOutputs();
    if (available_inputs < required_input_channels || available_outputs < required_output_channels) {
        std::ostringstream err;
        err << "AUDIOLAB.wing.reaper.virtualsoundcheck: REAPER audio device does not expose enough channels for "
            << output_type << " routing. Required by current selection: "
            << required_input_channels << " in / " << required_output_channels << " out, available: "
            << available_inputs << " in / " << available_outputs << " out.\n";
        Log(err.str());

        std::ostringstream msg;
        msg << "Selected " << output_type << " routing requires at least "
            << required_input_channels << " REAPER inputs";
        if (required_output_channels > 0) {
            msg << " and " << required_output_channels << " REAPER outputs";
        }
        msg << ".\n\n"
            << "Available now: " << available_inputs << " inputs / "
            << available_outputs << " outputs.\n\n"
            << "Please switch REAPER audio device/range or choose fewer sources.";
        ShowMessageBox(msg.str().c_str(), "AUDIOLAB.wing.reaper.virtualsoundcheck - Audio I/O Not Available", 0);
        return;
    }
    if (setup_soundcheck && !has_soundcheck_capable) {
        Log("Selected sources do not support ALT soundcheck. Proceeding in recording-only mode.\n");
    }
    
    // Show what will be configured
    Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Log("CONFIGURING WING CONSOLE FOR VIRTUAL SOUNDCHECK\n");
    Log("Output Mode: " + output_type + "\n");
    Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Log("\nSources to configure:\n");
    for (const auto& alloc : allocations) {
        std::string line = "  " + SourceIdentity(alloc.source_kind, alloc.source_number);
        if (alloc.is_stereo) {
            line += " (stereo) → " + output_type + " " + std::to_string(alloc.usb_start) + "-" + std::to_string(alloc.usb_end);
        } else {
            line += " (mono) → " + output_type + " " + std::to_string(alloc.usb_start);
        }
        line += "\n";
        Log(line.c_str());
    }
    Log("\n");
    
    // Apply output allocation
    if (setup_soundcheck && has_soundcheck_capable) {
        Log("Step 1/2: Configuring Wing " + output_type + " outputs and ALT sources...\n");
    } else {
        Log("Step 1/2: Configuring Wing " + output_type + " outputs (recording only, no soundcheck)...\n");
    }
    // Query USR routing data before configuring (to resolve routing chains)
    osc_handler_->QueryUserSignalInputs(48);
    osc_handler_->ApplyUSBAllocationAsAlt(allocations, selected_sources, output_mode, enable_soundcheck_setup);
    
    Log("\nStep 2/2: Creating REAPER tracks...\n");

    int track_index = 0;
    if (replace_existing) {
        ReaProject* proj = EnumProjects(-1, nullptr, 0);
        const int removed_existing_tracks = proj ? CountTracks(proj) : 0;
        track_manager_->ClearAllTracks();
        if (removed_existing_tracks > 0) {
            Log("Removed " + std::to_string(removed_existing_tracks) +
                " existing REAPER tracks before rebuilding the setup.\n");
        }
    } else {
        ReaProject* proj = EnumProjects(-1, nullptr, 0);
        track_index = proj ? CountTracks(proj) : 0;
        Log("Keeping the existing REAPER tracks and appending the new selection.\n");
    }

    Undo_BeginBlock();
    int created_tracks = 0;
    for (const auto& alloc : allocations) {
        if (!alloc.allocation_note.empty() && alloc.allocation_note.find("ERROR") != std::string::npos) {
            continue;
        }
        
        auto it = std::find_if(selected_sources.begin(), selected_sources.end(), [&](const SourceSelectionInfo& source) {
            return source.kind == alloc.source_kind && source.source_number == alloc.source_number;
        });
        if (it == selected_sources.end()) {
            continue;
        }
        int color_id = 0;
        if (it->kind == SourceKind::Channel) {
            auto ch_it = updated_channel_data.find(it->source_number);
            if (ch_it != updated_channel_data.end()) {
                color_id = ch_it->second.color;
            }
        }
        const std::string track_name = it->name.empty() ? SourceIdentity(*it) : it->name;
        
        MediaTrack* track = nullptr;
        if (alloc.is_stereo) {
            track = track_manager_->CreateStereoTrack(track_index, track_name, color_id);
            if (track) {
                track_manager_->SetTrackInput(track, alloc.usb_start - 1, 2);
                const std::string source_id = SourcePersistentId(*it);
                GetSetMediaTrackInfo_String(track, kTrackSourceIdExtKey, const_cast<char*>(source_id.c_str()), true);
                if (it->soundcheck_capable) {
                    track_manager_->SetTrackHardwareOutput(track, alloc.usb_start - 1, 2);
                } else {
                    track_manager_->ClearTrackHardwareOutputs(track);
                }
                SetMediaTrackInfo_Value(track, "B_MAINSEND", 0);
                std::string msg = "  ✓ Track " + std::to_string(created_tracks + 1) +
                                 ": " + track_name + " (stereo) IN " + output_type + " " +
                                 std::to_string(alloc.usb_start) + "-" + std::to_string(alloc.usb_end);
                if (it->soundcheck_capable) {
                    msg += " / OUT " + output_type + " " +
                           std::to_string(alloc.usb_start) + "-" + std::to_string(alloc.usb_end);
                }
                msg += "\n";
                Log(msg.c_str());
            }
        } else {
            track = track_manager_->CreateTrack(track_index, track_name, color_id);
            if (track) {
                track_manager_->SetTrackInput(track, alloc.usb_start - 1, 1);
                const std::string source_id = SourcePersistentId(*it);
                GetSetMediaTrackInfo_String(track, kTrackSourceIdExtKey, const_cast<char*>(source_id.c_str()), true);
                if (it->soundcheck_capable) {
                    track_manager_->SetTrackHardwareOutput(track, alloc.usb_start - 1, 1);
                } else {
                    track_manager_->ClearTrackHardwareOutputs(track);
                }
                SetMediaTrackInfo_Value(track, "B_MAINSEND", 0);
                std::string msg = "  ✓ Track " + std::to_string(created_tracks + 1) +
                                 ": " + track_name + " (mono) IN " + output_type + " " +
                                 std::to_string(alloc.usb_start);
                if (it->soundcheck_capable) {
                    msg += " / OUT " + output_type + " " + std::to_string(alloc.usb_start);
                }
                msg += "\n";
                Log(msg.c_str());
            }
        }
        
        if (track) {
            track_index++;
            created_tracks++;
        }
    }
    
    std::set<std::string> persisted_source_ids;
    if (!replace_existing) {
        persisted_source_ids.insert(config_.last_selected_source_ids.begin(), config_.last_selected_source_ids.end());
    }
    for (const auto& source : selected_sources) {
        persisted_source_ids.insert(SourcePersistentId(source));
    }
    config_.last_selected_source_ids.assign(persisted_source_ids.begin(), persisted_source_ids.end());
    config_.SaveToFile(WingConfig::GetConfigPath());

    Undo_EndBlock("AUDIOLAB.wing.reaper.virtualsoundcheck: Configure Virtual Soundcheck", UNDO_STATE_TRACKCFG);
    
    Log("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Log("✓ CONFIGURATION COMPLETE\n");
    Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    std::string final_msg = "Created " + std::to_string(created_tracks) + " REAPER tracks\n";
    Log(final_msg.c_str());
    if (has_soundcheck_capable) {
        Log("\nUse 'Toggle Soundcheck Mode' to enable/disable ALT sources.\n");
        Log("When enabled, channels receive audio from REAPER via USB.\n\n");
    } else {
        Log("\nSelected buses/matrices were configured for recording only.\n\n");
    }
}

void ReaperExtension::DisconnectFromWing() {
    if (!connected_) {
        return;
    }
    StopExternalRecorderFollow();
    last_known_reaper_recording_state_ = false;
    StopAutoRecordMonitor();
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Disconnecting...\n");
    
    if (osc_handler_) {
        osc_handler_->Stop();
        osc_handler_.reset();
    }
    
    connected_ = false;
    monitoring_enabled_ = false;
    status_message_ = "Disconnected";
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Disconnected\n");
}

std::vector<WingInfo> ReaperExtension::DiscoverWings(int timeout_ms) {
    return WingOSC::DiscoverWings(timeout_ms);
}

void ReaperExtension::RefreshTracks() {
    if (!connected_ || !osc_handler_) {
        ShowMessageBox(
            "Not connected to Wing console.\n"
            "Please connect first.",
            "AUDIOLAB.wing.reaper.virtualsoundcheck",
            0
        );
        return;
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Refreshing tracks...\n");
    
    // Re-query channels
    osc_handler_->QueryAllChannels(config_.channel_count);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // Wait for OSC responses
    
    // Update existing tracks or create new ones
    const auto& channel_data = osc_handler_->GetChannelData();
    int track_count = track_manager_->CreateTracksFromChannelData(channel_data);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Refreshed %d tracks", track_count);
    Log(msg);
    Log("\n");
}

void ReaperExtension::ShowSettings() {
    #ifdef __APPLE__
    // Use native macOS dialog for settings
    char ip_buffer[256];
    
    strncpy(ip_buffer, config_.wing_ip.c_str(), sizeof(ip_buffer) - 1);
    ip_buffer[sizeof(ip_buffer) - 1] = '\0';
    
    // Show native Cocoa dialog
    if (ShowSettingsDialog(config_.wing_ip.c_str(),
                          ip_buffer, sizeof(ip_buffer))) {
        // Validate IP
        std::string new_ip = ip_buffer;
        if (new_ip.empty() || new_ip.length() > 15) {
            ShowMessageBox("Invalid IP address.\nPlease use format: 192.168.0.1", 
                          "AUDIOLAB.wing.reaper.virtualsoundcheck - Error", 0);
            return;
        }
        // Update configuration
        config_.wing_ip = new_ip;
        config_.wing_port = 2223;
        config_.listen_port = 2223;
        
        // Save to file
        const std::string config_path = WingConfig::GetConfigPath();
        if (config_.SaveToFile(config_path)) {
            char success_msg[256];
            snprintf(success_msg, sizeof(success_msg),
                "Settings saved successfully!\n\n"
                "IP: %s\n"
                "OSC Port: 2223\n\n"
                "Changes will apply on next connection.",
                config_.wing_ip.c_str());
            
            ShowMessageBox(success_msg, "AUDIOLAB.wing.reaper.virtualsoundcheck - Settings Saved", 0);
            Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Settings updated from dialog\n");
        } else {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg),
                      "Failed to save settings to:\n%s\n\nPlease check file permissions.",
                      config_path.c_str());
            ShowMessageBox(error_msg,
                          "AUDIOLAB.wing.reaper.virtualsoundcheck - Error", 0);
        }
    }
    #else
    // Fallback for non-macOS platforms
    char settings_msg[512];
    snprintf(settings_msg, sizeof(settings_msg), 
        "AUDIOLAB.wing.reaper.virtualsoundcheck Settings\n\n"
        "Current Configuration:\n"
        "  Wing IP: %s\n"
        "  OSC Port: 2223\n"
        "\nEdit config.json to change settings.\n",
        config_.wing_ip.c_str());
    
    ShowMessageBox(settings_msg, "AUDIOLAB.wing.reaper.virtualsoundcheck - Settings", 0);
    #endif
}

void ReaperExtension::EnableMonitoring(bool enable) {
    monitoring_enabled_ = enable;
    
    if (enable) {
        status_message_ = "Monitoring active";
    } else {
        status_message_ = "Monitoring inactive";
    }
}

int ReaperExtension::GetProjectTrackCount() const {
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    return proj ? CountTracks(proj) : 0;
}

void ReaperExtension::StartAutoRecordMonitor() {
    if (!config_.auto_record_enabled || auto_record_monitor_running_) {
        return;
    }
    auto_record_monitor_running_ = true;
    auto_record_monitor_thread_ = std::make_unique<std::thread>(&ReaperExtension::MonitorAutoRecordLoop, this);
}

void ReaperExtension::StopAutoRecordMonitor() {
    if (!auto_record_monitor_running_) {
        return;
    }
    auto_record_monitor_running_ = false;
    if (auto_record_monitor_thread_ && auto_record_monitor_thread_->joinable()) {
        auto_record_monitor_thread_->join();
    }
    auto_record_monitor_thread_.reset();
    auto_record_started_by_plugin_ = false;
    StopWarningFlash();
    ClearLayerState();
}

void ReaperExtension::ApplyAutoRecordSettings() {
    StopAutoRecordMonitor();
    if (!config_.auto_record_enabled || !config_.sd_auto_record_with_reaper) {
        StopExternalRecorderFollow();
        last_known_reaper_recording_state_ = false;
    }
    if (connected_ && config_.auto_record_enabled) {
        StartAutoRecordMonitor();
    }
}

void ReaperExtension::PauseAutoRecordForSetup() {
    StopAutoRecordMonitor();
}

double ReaperExtension::GetMaxArmedTrackPeak() const {
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    if (!proj) {
        return 0.0;
    }

    const int track_count = CountTracks(proj);
    double max_peak = 0.0;
    int start_i = 0;
    int end_i = track_count;
    if (config_.auto_record_monitor_track > 0) {
        start_i = std::min(track_count, std::max(0, config_.auto_record_monitor_track - 1));
        end_i = std::min(track_count, start_i + 1);
    }

    const bool specific_track_mode = (config_.auto_record_monitor_track > 0);
    for (int i = start_i; i < end_i; ++i) {
        MediaTrack* track = GetTrack(proj, i);
        if (!track) {
            continue;
        }

        if (!specific_track_mode) {
            const int rec_arm = static_cast<int>(GetMediaTrackInfo_Value(track, "I_RECARM"));
            const int rec_mon = static_cast<int>(GetMediaTrackInfo_Value(track, "I_RECMON"));
            if (rec_arm <= 0 || rec_mon <= 0) {
                continue;
            }
        }

        const double peak_l = Track_GetPeakInfo(track, 0);
        const double peak_r = Track_GetPeakInfo(track, 1);
        max_peak = std::max(max_peak, std::max(peak_l, peak_r));
    }
    return max_peak;
}

void ReaperExtension::MonitorAutoRecordLoop() {
    const double threshold_lin = std::pow(10.0, config_.auto_record_threshold_db / 20.0);
    const auto poll_interval = std::chrono::milliseconds(std::max(10, config_.auto_record_poll_ms));
    const auto attack_needed = std::chrono::milliseconds(std::max(50, config_.auto_record_attack_ms));
    const auto hold_needed = std::chrono::milliseconds(std::max(0, config_.auto_record_hold_ms));
    const auto release_needed = std::chrono::milliseconds(std::max(100, config_.auto_record_release_ms));
    const auto min_record_time = std::chrono::milliseconds(std::max(0, config_.auto_record_min_record_ms));

    auto above_since = std::chrono::steady_clock::time_point{};
    auto below_since = std::chrono::steady_clock::time_point{};
    auto record_started_at = std::chrono::steady_clock::time_point{};
    auto last_signal_at = std::chrono::steady_clock::time_point{};
    auto last_warning_event_at = std::chrono::steady_clock::time_point{};
    auto last_record_led_step_at = std::chrono::steady_clock::time_point{};
    size_t record_led_step = 0;

    while (auto_record_monitor_running_) {
        const auto now = std::chrono::steady_clock::now();
        const long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now.time_since_epoch())
                                     .count();
        double peak = GetMaxArmedTrackPeak();
        const bool above = peak >= threshold_lin;
        const int play_state = GetPlayState();
        const bool is_recording = (play_state & kReaperPlayStateRecordingBit) != 0;
        const bool is_playing = (play_state & kReaperPlayStatePlayingBit) != 0;
        if (above) {
            last_signal_at = now;
        }

        // Short suppression window after manual Record CC press:
        // avoid warning race before REAPER reports recording state.
        if (!is_recording && now_ms < manual_record_suppress_until_ms_.load()) {
            above_since = {};
            below_since = {};
            if (warning_flash_running_) {
                StopWarningFlash(true);
                ClearLayerState();
            }
            std::this_thread::sleep_for(poll_interval);
            continue;
        }

        // Playback should suppress warning mode entirely.
        if (is_playing && !is_recording) {
            above_since = {};
            below_since = {};
            if (warning_flash_running_) {
                StopWarningFlash(true);
                ClearLayerState();
            }
            std::this_thread::sleep_for(poll_interval);
            continue;
        }

        if (!is_recording) {
            // Auto-start was requested but REAPER has not yet entered record:
            // suppress retrigger to prevent duplicate takes/toggle churn.
            if (auto_record_started_by_plugin_) {
                if (record_started_at.time_since_epoch().count() == 0) {
                    record_started_at = now;
                }
                if (now - record_started_at < std::chrono::milliseconds(1500)) {
                    std::this_thread::sleep_for(poll_interval);
                    continue;
                }
                // Recover if recording did not start in time.
                auto_record_started_by_plugin_ = false;
            }

            record_led_step = 0;
            last_record_led_step_at = std::chrono::steady_clock::time_point{};
            if (above) {
                if (above_since.time_since_epoch().count() == 0) {
                    above_since = now;
                } else if (now - above_since >= attack_needed) {
                    if (!warning_flash_running_) {
                        StartWarningFlash();
                        SetWarningLayerState();
                    }
                    if (config_.auto_record_warning_only) {
                        const bool hold_active = last_warning_event_at.time_since_epoch().count() != 0 &&
                                                 (now - last_warning_event_at < hold_needed);
                        if (hold_active) {
                            above_since = {};
                            below_since = {};
                            std::this_thread::sleep_for(poll_interval);
                            continue;
                        }
                        last_warning_event_at = now;
                        SendOscToWing(config_, config_.osc_warning_path, 1);
                    } else {
                        if (warning_flash_running_) {
                            StopWarningFlash();
                        }
                        SetRecordingLayerState();
                        pending_record_start_ = true;
                        auto_record_started_by_plugin_ = true;
                        record_started_at = std::chrono::steady_clock::now();
                        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Auto-trigger threshold met; queued REAPER record start.\n");
                        SendOscToWing(config_, config_.osc_start_path, 1);
                    }
                    above_since = {};
                    below_since = {};
                }
            } else {
                above_since = {};
                const bool hold_expired = last_signal_at.time_since_epoch().count() == 0 ||
                                          (now - last_signal_at >= hold_needed);
                if (warning_flash_running_ && hold_expired) {
                    StopWarningFlash();
                    ClearLayerState();
                }
            }
        } else {
            // Recording state (manual or auto): warning system must be inactive.
            manual_record_suppress_until_ms_ = 0;
            StopWarningFlash(true);
            above_since = {};
            last_warning_event_at = {};
            if (!auto_record_started_by_plugin_) {
                // Manual recording: keep warning disabled, but do not apply auto-record
                // recording visuals (they would overwrite manual play/record flashes).
                std::this_thread::sleep_for(poll_interval);
                continue;
            }

            SetRecordingLayerState();
            if (osc_handler_ && midi_actions_enabled_) {
                const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
                if (last_record_led_step_at.time_since_epoch().count() == 0 ||
                    (now - last_record_led_step_at) >= std::chrono::milliseconds(440)) {
                    // Slower flowing pattern:
                    // 1 -> 12 -> 123 -> 1234 -> 234 -> 34 -> 4 -> off -> repeat
                    record_led_step = (record_led_step + 1) % 8;
                    last_record_led_step_at = now;
                }
                static const int kMasks[8] = {
                    0b0001, 0b0011, 0b0111, 0b1111, 0b1110, 0b1100, 0b1000, 0b0000
                };
                const int mask = kMasks[record_led_step];
                for (int b = 1; b <= 4; ++b) {
                    const bool on = (mask & (1 << (b - 1))) != 0;
                    osc_handler_->SetUserControlLed(layer, b, on);
                }
            }

            if (above) {
                below_since = {};
            } else {
                if (below_since.time_since_epoch().count() == 0) {
                    below_since = now;
                } else if (now - below_since >= release_needed &&
                           (last_signal_at.time_since_epoch().count() == 0 || now - last_signal_at >= hold_needed) &&
                           now - record_started_at >= min_record_time) {
                    pending_record_stop_ = true;
                    SendOscToWing(config_, config_.osc_stop_path, 0);
                    auto_record_started_by_plugin_ = false;
                    ClearLayerState();
                    above_since = {};
                    below_since = {};
                }
            }
        }

        std::this_thread::sleep_for(poll_interval);
    }
}

void ReaperExtension::SetWarningLayerState() {
    if (!osc_handler_ || !midi_actions_enabled_) {
        return;
    }
    if (layer_state_mode_.load() == 1) {
        return;
    }
    layer_state_mode_ = 1;
    const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
    osc_handler_->SetActiveUserControlLayer(layer);
    for (int b = 1; b <= 4; ++b) {
        osc_handler_->QueryUserControlColor(layer, b);
    }
    osc_handler_->QueryUserControlRotaryText(layer, 1);
    osc_handler_->SetUserControlRotaryName(layer, 1, midi_actions_enabled_ ? "REAPER:" : "");
    osc_handler_->SetUserControlRotaryName(layer, 2, "RECORDING");
    osc_handler_->SetUserControlRotaryName(layer, 3, "NOT");
    osc_handler_->SetUserControlRotaryName(layer, 4, "STARTED!!");
}

void ReaperExtension::SetRecordingLayerState() {
    if (!osc_handler_ || !midi_actions_enabled_) {
        return;
    }
    if (layer_state_mode_.load() == 2) {
        return;
    }
    layer_state_mode_ = 2;
    const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
    osc_handler_->SetActiveUserControlLayer(layer);
    osc_handler_->QueryUserControlRotaryText(layer, 1);
    osc_handler_->SetUserControlRotaryName(layer, 1, midi_actions_enabled_ ? "REAPER:" : "");
    osc_handler_->SetUserControlRotaryName(layer, 2, "RECORDING");
    osc_handler_->SetUserControlRotaryName(layer, 3, "STARTED");
    osc_handler_->SetUserControlRotaryName(layer, 4, "....");
    const int recording_color = 6; // Force green for recording
    for (int b = 1; b <= 4; ++b) {
        osc_handler_->SetUserControlColor(layer, b, recording_color);
        osc_handler_->SetUserControlLed(layer, b, false);
    }
}

void ReaperExtension::ClearLayerState() {
    if (!osc_handler_) {
        return;
    }
    layer_state_mode_ = 0;
    const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
    for (int b = 1; b <= 4; ++b) {
        osc_handler_->SetUserControlLed(layer, b, false);
    }
    osc_handler_->SetUserControlRotaryName(layer, 1, midi_actions_enabled_ ? "REAPER:" : "");
    osc_handler_->SetUserControlRotaryName(layer, 2, "");
    osc_handler_->SetUserControlRotaryName(layer, 3, "");
    osc_handler_->SetUserControlRotaryName(layer, 4, "");
}

void ReaperExtension::ApplyMidiShortcutButtonLabels() {
    if (!osc_handler_) {
        return;
    }
    const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
    osc_handler_->SetUserControlRotaryName(layer, 1, "REAPER:");
    // Row 1
    osc_handler_->SetUserControlButtonName(layer, 1, "PLAY");
    osc_handler_->SetUserControlButtonName(layer, 2, "RECORD");
    osc_handler_->SetUserControlButtonName(layer, 3, "SOUNDCHECK");
    osc_handler_->SetUserControlButtonName(layer, 4, "STOP");
    // Row 2
    osc_handler_->SetUserControlButtonName(layer, 1, "SET MARKER", true);
    osc_handler_->SetUserControlButtonName(layer, 2, "PREV MARKER", true);
    osc_handler_->SetUserControlButtonName(layer, 3, "NEXT MARKER", true);
}

void ReaperExtension::ClearMidiShortcutButtonLabels() {
    if (!osc_handler_) {
        return;
    }
    const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
    for (int b = 1; b <= 4; ++b) {
        osc_handler_->SetUserControlButtonName(layer, b, "");
    }
    for (int b = 1; b <= 3; ++b) {
        osc_handler_->SetUserControlButtonName(layer, b, "", true);
    }
}

void ReaperExtension::ApplyMidiShortcutButtonCommands() {
    if (!osc_handler_) {
        return;
    }
    const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
    // Map all assigned CC buttons as push buttons, not toggles.
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 1, 1, MIDI_ACTIONS[0].cc_number, 0, false, false);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 2, 1, MIDI_ACTIONS[1].cc_number, 0, false, false);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 3, 1, MIDI_ACTIONS[2].cc_number, 0, false, false);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 4, 1, MIDI_ACTIONS[3].cc_number, 0, false, false);
    // Bottom row (bd): set/prev/next marker on buttons 1..3
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 1, 1, MIDI_ACTIONS[4].cc_number, 0, true, false);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 2, 1, MIDI_ACTIONS[5].cc_number, 0, true, false);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 3, 1, MIDI_ACTIONS[6].cc_number, 0, true, false);
    // Force all mapped button values off so enabling mappings does not trigger stale toggle states.
    for (int b = 1; b <= 4; ++b) {
        osc_handler_->SetUserControlButtonValue(layer, b, 0, false);
    }
    for (int b = 1; b <= 3; ++b) {
        osc_handler_->SetUserControlButtonValue(layer, b, 0, true);
    }
    // Keep shortcut buttons unlit by default; warning/record states drive LEDs explicitly.
    for (int pass = 0; pass < 4; ++pass) {
        for (int b = 1; b <= 4; ++b) {
            osc_handler_->SetUserControlColor(layer, b, 0);
            osc_handler_->SetUserControlLed(layer, b, false);
            osc_handler_->SetUserControlButtonLed(layer, b, false, false);
            osc_handler_->SetUserControlButtonLed(layer, b, false, true);
        }
        // Wing appears to latch some user control LED state; retry clear a few times.
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    // Final delayed pass to clear any late-applied console defaults.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for (int b = 1; b <= 4; ++b) {
        osc_handler_->SetUserControlLed(layer, b, false);
        osc_handler_->SetUserControlButtonLed(layer, b, false, false);
        osc_handler_->SetUserControlButtonLed(layer, b, false, true);
    }
}

void ReaperExtension::ClearMidiShortcutButtonCommands() {
    if (!osc_handler_) {
        return;
    }
    const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
    for (int b = 1; b <= 4; ++b) {
        osc_handler_->ClearUserControlButtonCommand(layer, b, false);
    }
    for (int b = 1; b <= 3; ++b) {
        osc_handler_->ClearUserControlButtonCommand(layer, b, true);
    }
}

void ReaperExtension::StartWarningFlash() {
    if (warning_flash_running_ || !osc_handler_ || !midi_actions_enabled_) {
        return;
    }
    warning_flash_running_ = true;
    warning_flash_thread_ = std::make_unique<std::thread>(&ReaperExtension::WarningFlashLoop, this);
}

void ReaperExtension::StopWarningFlash(bool force) {
    (void)force;
    if (!warning_flash_running_) {
        return;
    }
    warning_flash_running_ = false;
    if (warning_flash_thread_ && warning_flash_thread_->joinable()) {
        warning_flash_thread_->join();
    }
    warning_flash_thread_.reset();
    if (osc_handler_) {
        const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
        for (int b = 1; b <= 4; ++b) {
            osc_handler_->SetUserControlLed(layer, b, false);
        }
    }
}

void ReaperExtension::WarningFlashLoop() {
    if (!osc_handler_ || !midi_actions_enabled_) {
        warning_flash_running_ = false;
        return;
    }
    const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
    int active_color = osc_handler_->GetCachedUserControlColor(layer, 1, config_.warning_flash_cc_color);
    for (int b = 1; b <= 4; ++b) {
        osc_handler_->SetUserControlColor(layer, b, active_color);
    }

    const int sequence[] = {1, 2, 3, 4, 3, 2};
    size_t seq_idx = 0;
    bool warning_text_visible = true;
    auto last_warning_text_toggle = std::chrono::steady_clock::time_point{};
    int color_poll_div = 0;
    while (warning_flash_running_) {
        const auto now = std::chrono::steady_clock::now();
        // Refresh color from button 1 assignment every ~1s (8 * 120ms).
        if (++color_poll_div >= 8) {
            color_poll_div = 0;
            osc_handler_->QueryUserControlColor(layer, 1);
            const int latest = osc_handler_->GetCachedUserControlColor(layer, 1, active_color);
            if (latest != active_color) {
                active_color = latest;
                for (int b = 1; b <= 4; ++b) {
                    osc_handler_->SetUserControlColor(layer, b, active_color);
                }
            }
        }
        const int active = sequence[seq_idx];
        for (int b = 1; b <= 4; ++b) {
            osc_handler_->SetUserControlLed(layer, b, b == active);
        }
        // Blink warning text on encoders 2..4 at 1/4 of LED chase speed (480ms).
        if (last_warning_text_toggle.time_since_epoch().count() == 0 ||
            (now - last_warning_text_toggle) >= std::chrono::milliseconds(480)) {
            if (warning_text_visible) {
                osc_handler_->SetUserControlRotaryName(layer, 2, "RECORDING");
                osc_handler_->SetUserControlRotaryName(layer, 3, "NOT");
                osc_handler_->SetUserControlRotaryName(layer, 4, "STARTED!!");
            } else {
                osc_handler_->SetUserControlRotaryName(layer, 2, "");
                osc_handler_->SetUserControlRotaryName(layer, 3, "");
                osc_handler_->SetUserControlRotaryName(layer, 4, "");
            }
            warning_text_visible = !warning_text_visible;
            last_warning_text_toggle = now;
        }
        seq_idx = (seq_idx + 1) % (sizeof(sequence) / sizeof(sequence[0]));
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }
}

void ReaperExtension::RouteMainLRToCardForSDRecording() {
    if (!connected_ || !osc_handler_) {
        ShowMessageBox(
            "Not connected to Wing console.\nPlease connect first.",
            "AUDIOLAB.wing.reaper.virtualsoundcheck",
            0
        );
        return;
    }

    const int left_input = std::max(1, config_.sd_lr_left_input);
    const int right_input = std::max(1, config_.sd_lr_right_input);
    const std::string group = config_.sd_lr_group.empty() ? "MAIN" : config_.sd_lr_group;

    const bool usb_recorder = RecorderTargetKey(config_) == "USBREC";
    Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: Routing Main LR to ") +
        RecorderTargetLabel(config_) + " 1/2...\n");
    if (usb_recorder) {
        osc_handler_->SetRecorderOutputSource(1, group, left_input);
        osc_handler_->SetRecorderOutputSource(2, group, right_input);
        osc_handler_->SetRecorderOutputName(1, "Main L");
        osc_handler_->SetRecorderOutputName(2, "Main R");
    } else {
        osc_handler_->SetWLiveRecordTrackCount(1, 4);
        osc_handler_->SetCardOutputSource(1, group, left_input);
        osc_handler_->SetCardOutputSource(2, group, right_input);
        osc_handler_->SetCardOutputName(1, "Main L");
        osc_handler_->SetCardOutputName(2, "Main R");
    }

    const std::string msg = std::string("Configured ") + RecorderTargetLabel(config_) +
                            " outputs: 1=" + group + ":" + std::to_string(left_input) +
                            ", 2=" + group + ":" + std::to_string(right_input) + "\n";
    Log(msg);
    const std::string verify_msg = std::string("Requested ") + RecorderTargetLabel(config_) +
                                   " 1/2 routing from Main LR.\nVerify the routing on WING before starting recorder capture.";
    ShowMessageBox(
        verify_msg.c_str(),
        "AUDIOLAB.wing.reaper.virtualsoundcheck",
        0
    );
}

void ReaperExtension::ApplyRecorderRoutingNoDialog() {
    if (!osc_handler_) {
        return;
    }
    const int left_input = std::max(1, config_.sd_lr_left_input);
    const int right_input = std::max(1, config_.sd_lr_right_input);
    const std::string group = config_.sd_lr_group.empty() ? "MAIN" : config_.sd_lr_group;
    if (RecorderTargetKey(config_) == "USBREC") {
        osc_handler_->SetRecorderOutputSource(1, group, left_input);
        osc_handler_->SetRecorderOutputSource(2, group, right_input);
        osc_handler_->SetRecorderOutputName(1, "Main L");
        osc_handler_->SetRecorderOutputName(2, "Main R");
    } else {
        osc_handler_->SetWLiveRecordTrackCount(1, 4);
        osc_handler_->SetCardOutputSource(1, group, left_input);
        osc_handler_->SetCardOutputSource(2, group, right_input);
        osc_handler_->SetCardOutputName(1, "Main L");
        osc_handler_->SetCardOutputName(2, "Main R");
    }
}

void ReaperExtension::StartExternalRecorderFollow() {
    if (!config_.auto_record_enabled || !config_.sd_auto_record_with_reaper || !connected_ || !osc_handler_) {
        return;
    }
    if (external_recorder_started_by_plugin_.exchange(true)) {
        return;
    }

    ApplyRecorderRoutingNoDialog();
    Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: ") + RecorderTargetLabel(config_) +
        " routing applied (best effort; verify on WING).\n");
    if (RecorderTargetKey(config_) == "USBREC") {
        osc_handler_->StartUSBRecorder();
    } else {
        osc_handler_->StartSDRecorder();
    }
    Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: ") + RecorderTargetLabel(config_) +
        " start requested for plugin auto-trigger recording (best effort OSC; verify recorder state on WING).\n");

    std::string status_detail;
    if (PollRecorderStarted(config_, osc_handler_.get(), status_detail)) {
        Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: Confirmed ") +
            RecorderTargetLabel(config_) + " started. " + status_detail + "\n");
    } else {
        Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: ") + RecorderTargetLabel(config_) +
            " did not confirm a recording state after start request. " + status_detail + "\n");
    }
}

void ReaperExtension::StopExternalRecorderFollow() {
    if (!external_recorder_started_by_plugin_.exchange(false)) {
        return;
    }
    if (!osc_handler_) {
        return;
    }
    if (RecorderTargetKey(config_) == "USBREC") {
        osc_handler_->StopUSBRecorder();
    } else {
        osc_handler_->StopSDRecorder();
    }
    Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: ") + RecorderTargetLabel(config_) +
        " stop requested for plugin auto-trigger recording (best effort OSC; verify recorder state on WING).\n");

    std::string status_detail;
    if (PollRecorderStopped(config_, osc_handler_.get(), status_detail)) {
        Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: Confirmed ") +
            RecorderTargetLabel(config_) + " stopped. " + status_detail + "\n");
    } else {
        Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: ") + RecorderTargetLabel(config_) +
            " did not confirm a stopped state after stop request. " + status_detail + "\n");
    }
}

void ReaperExtension::SyncExternalRecorderWithReaperState(bool is_recording_now) {
    const bool was_recording = last_known_reaper_recording_state_.exchange(is_recording_now);

    if (!config_.auto_record_enabled || !config_.sd_auto_record_with_reaper || !connected_ || !osc_handler_) {
        if (external_recorder_started_by_plugin_) {
            StopExternalRecorderFollow();
        }
        return;
    }

    if (is_recording_now && !was_recording) {
        if (auto_record_started_by_plugin_) {
            Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: REAPER entered record from auto-trigger; starting ") +
                RecorderTargetLabel(config_) + ".\n");
            StartExternalRecorderFollow();
        } else {
            Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: REAPER entered record, but no plugin-owned auto-trigger session was active; not starting ") +
                RecorderTargetLabel(config_) + ".\n");
        }
    } else if (!is_recording_now && was_recording && external_recorder_started_by_plugin_) {
        Log(std::string("AUDIOLAB.wing.reaper.virtualsoundcheck: REAPER left record; stopping ") +
            RecorderTargetLabel(config_) + ".\n");
        StopExternalRecorderFollow();
    }
}

double ReaperExtension::ReadCurrentTriggerLevel() {
    return GetMaxArmedTrackPeak();
}

void ReaperExtension::ConfigureVirtualSoundcheck() {
    if (!connected_ || !osc_handler_) {
        ShowMessageBox(
            "Please connect to Wing console first",
            "AUDIOLAB.wing.reaper.virtualsoundcheck - Virtual Soundcheck",
            0
        );
        return;
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Configuring Virtual Soundcheck...\n");

    auto sources = GetAvailableSources();
    if (sources.empty()) {
        ShowMessageBox(
            "No selectable sources available.\nPlease refresh tracks first.",
            "AUDIOLAB.wing.reaper.virtualsoundcheck - Virtual Soundcheck",
            0
        );
        return;
    }

    std::set<int> included_set = ParseChannelSelection(config_.include_channels, 48);
    std::set<int> excluded_set = ParseChannelSelection(config_.exclude_channels, 48);
    bool any_selected = false;
    for (auto& source : sources) {
        bool selected = (source.kind == SourceKind::Channel);
        if (source.kind == SourceKind::Channel) {
            if (!included_set.empty()) {
                selected = included_set.find(source.source_number) != included_set.end();
            }
            if (excluded_set.find(source.source_number) != excluded_set.end()) {
                selected = false;
            }
        }
        source.selected = selected;
        any_selected = any_selected || selected;
    }

    if (!any_selected) {
        for (auto& source : sources) {
            source.selected = (source.kind == SourceKind::Channel);
        }
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: No channels remained after filtering. Using all available channels.\n");
    }

    SetupSoundcheckFromSelection(sources, true);
}

void ReaperExtension::ToggleSoundcheckMode() {
    if (!connected_ || !osc_handler_) {
        ShowMessageBox(
            "Please connect to Wing console first",
            "AUDIOLAB.wing.reaper.virtualsoundcheck - Soundcheck Mode",
            0
        );
        return;
    }
    
    // Toggle the state
    soundcheck_mode_enabled_ = !soundcheck_mode_enabled_;
    
    // Apply to all channels
    osc_handler_->SetAllChannelsAltEnabled(soundcheck_mode_enabled_);
    
    if (soundcheck_mode_enabled_) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Soundcheck Mode ENABLED - Channels using USB input from REAPER\n");
        status_message_ = "Soundcheck Mode ON";
    } else {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Soundcheck Mode DISABLED - Channels using primary sources\n");
        status_message_ = "Soundcheck Mode OFF";
    }
}

void ReaperExtension::OnChannelDataReceived(const ChannelInfo& channel) {
    if (monitoring_enabled_) {
        // In monitoring mode, update tracks in real-time
        auto existing_tracks = track_manager_->FindExistingWingTracks();
        
        // Find track for this channel and update it
        if ((size_t)channel.channel_number <= existing_tracks.size()) {
            MediaTrack* track = existing_tracks[channel.channel_number - 1];
            track_manager_->UpdateTrack(track, channel);
        }
    }
}

// ============================================================================
// MIDI Action Mapping
// ============================================================================

void ReaperExtension::EnableWingMidiDevice() {
    int num_inputs = GetNumMIDIInputs();
    bool found_wing = false;
    
    char msg[1024];
    snprintf(msg, sizeof(msg), 
             "Checking for Wing MIDI device...\n"
             "Found %d MIDI input device(s):\n", num_inputs);
    Log(msg);
    
    for (int i = 0; i < num_inputs; i++) {
        char device_name[256];
        
        if (GetMIDIInputName(i, device_name, sizeof(device_name))) {
            // Log all devices
            snprintf(msg, sizeof(msg), "  [%d] %s\n", i, device_name);
            Log(msg);
            
            // Look for Wing device (case-insensitive search)
            std::string name_lower = device_name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
            
            if (name_lower.find("wing") != std::string::npos) {
                found_wing = true;
                snprintf(msg, sizeof(msg), 
                         "\n✓ Wing MIDI device detected: %s\n"
                         "⚠ Make sure it's ENABLED in: Preferences → Audio → MIDI Devices\n"
                         "  (Enable 'Input' checkbox for this device)\n\n", 
                         device_name);
                Log(msg);
            }
        }
    }
    
    if (!found_wing) {
        Log("\n⚠ Warning: No Wing MIDI device found!\n"
            "For MIDI actions to work:\n"
            "1. Connect Wing to computer via USB or network MIDI\n"
            "2. Enable device in: Preferences → Audio → MIDI Devices\n\n");
    }
}

void ReaperExtension::UnregisterMidiShortcuts() {
    const auto kb_paths = GetReaperKeymapPaths();
    for (const auto& kb_ini_path : kb_paths) {
        std::ifstream in_file(kb_ini_path);
        if (!in_file.is_open()) {
            continue;
        }
        std::string content;
        std::string line;
        while (std::getline(in_file, line)) {
            bool is_wing_midi = false;
            for (const auto& action : MIDI_ACTIONS) {
                int cc_encoded = action.cc_number + 128;
                std::string wing_line = "KEY 176 " + std::to_string(cc_encoded);
                if (line.find(wing_line) != std::string::npos) {
                    is_wing_midi = true;
                    break;
                }
            }
            if (!is_wing_midi) {
                content += line + "\n";
            }
        }
        in_file.close();
        std::ofstream out_file(kb_ini_path);
        out_file << content;
        out_file.close();
        TouchFile(kb_ini_path);
    }
}

void ReaperExtension::EnableMidiActions(bool enable) {
    if (enable) {
        const int state_before = GetPlayState();
        const bool was_stopped_before = ((state_before & kReaperPlayStatePlayingBit) == 0) &&
                                        ((state_before & kReaperPlayStateRecordingBit) == 0);
        ReaProject* proj = EnumProjects(-1, nullptr, 0);
        const double pos_before = proj ? GetPlayPositionEx(proj) : GetPlayPosition();
        if (was_stopped_before) {
            transport_guard_from_stopped_state_ = true;
            transport_guard_restore_pos_ = pos_before;
            transport_guard_until_ms_ = SteadyNowMs() + 7000;
        } else {
            transport_guard_from_stopped_state_ = false;
            transport_guard_until_ms_ = 0;
        }

        // Do not process incoming MIDI while assignment commands are being pushed to Wing.
        suppress_midi_processing_ = true;
        const long long suppress_until = SteadyNowMs() + 5000;
        suppress_all_cc_until_ms_ = suppress_until;
        suppress_play_cc_until_ms_ = suppress_until;
        suppress_record_cc_until_ms_ = suppress_until;
        pending_midi_command_ = 0;
        pending_record_start_ = false;
        pending_record_stop_ = false;
        pending_toggle_soundcheck_mode_ = false;
        // Ensure legacy keymap shortcuts are removed; use plugin MIDI handling only.
        StopMidiCapture();
        UnregisterMidiShortcuts();
        ApplyMidiShortcutButtonLabels();
        ApplyMidiShortcutButtonCommands();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        // Start listening only after assignment is sent.
        StartMidiCapture();
        // Wing may emit CC echoes while applying button command configuration.
        const long long post_apply_suppress_until = SteadyNowMs() + 5000;
        suppress_play_cc_until_ms_ = post_apply_suppress_until;
        suppress_record_cc_until_ms_ = post_apply_suppress_until;
        suppress_all_cc_until_ms_ = post_apply_suppress_until;
        midi_actions_enabled_ = true;
        suppress_midi_processing_ = false;
    } else {
        if (!midi_actions_enabled_) {
            return;  // Already disabled
        }
        StopWarningFlash(true);
        StopManualTransportFlash();
        ClearLayerState();
        midi_actions_enabled_ = false;
        StopMidiCapture();
        UnregisterMidiShortcuts();
        ClearMidiShortcutButtonCommands();
        ClearMidiShortcutButtonLabels();
    }
    
    //Update config
    config_.configure_midi_actions = enable;
    config_.SaveToFile(WingConfig::GetConfigPath());
}

void ReaperExtension::SyncMidiActionsToWing() {
    if (!midi_actions_enabled_ || !connected_ || !osc_handler_) {
        return;
    }
    const int state_before = GetPlayState();
    const bool was_stopped_before = ((state_before & kReaperPlayStatePlayingBit) == 0) &&
                                    ((state_before & kReaperPlayStateRecordingBit) == 0);
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    const double pos_before = proj ? GetPlayPositionEx(proj) : GetPlayPosition();
    if (was_stopped_before) {
        transport_guard_from_stopped_state_ = true;
        transport_guard_restore_pos_ = pos_before;
        transport_guard_until_ms_ = SteadyNowMs() + 7000;
    } else {
        transport_guard_from_stopped_state_ = false;
        transport_guard_until_ms_ = 0;
    }

    suppress_midi_processing_ = true;
    const long long suppress_until = SteadyNowMs() + 5000;
    suppress_all_cc_until_ms_ = suppress_until;
    suppress_play_cc_until_ms_ = suppress_until;
    suppress_record_cc_until_ms_ = suppress_until;
    pending_midi_command_ = 0;
    pending_record_start_ = false;
    pending_record_stop_ = false;
    pending_toggle_soundcheck_mode_ = false;
    // Temporarily pause capture while pushing commands to Wing.
    // Keep keymap shortcuts disabled (plugin MIDI handling only).
    StopMidiCapture();
    UnregisterMidiShortcuts();
    ApplyMidiShortcutButtonLabels();
    ApplyMidiShortcutButtonCommands();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    StartMidiCapture();
    // Wing may emit CC echoes while applying button command configuration.
    const long long post_apply_suppress_until = SteadyNowMs() + 5000;
    suppress_play_cc_until_ms_ = post_apply_suppress_until;
    suppress_record_cc_until_ms_ = post_apply_suppress_until;
    suppress_all_cc_until_ms_ = post_apply_suppress_until;
    suppress_midi_processing_ = false;
}

void ReaperExtension::TriggerManualTransportFlash(int color_index) {
    if (!connected_ || !osc_handler_ || !midi_actions_enabled_) {
        return;
    }
    std::unique_ptr<std::thread> thread_to_join;
    {
        std::lock_guard<std::mutex> lock(manual_transport_flash_mutex_);
        manual_transport_flash_running_ = false;
        if (manual_transport_flash_thread_) {
            thread_to_join = std::move(manual_transport_flash_thread_);
        }
    }
    if (thread_to_join && thread_to_join->joinable()) {
        thread_to_join->join();
    }

    try {
        manual_transport_flash_running_ = true;
        manual_transport_flash_thread_ = std::make_unique<std::thread>([this, color_index]() {
            if (!osc_handler_) {
                manual_transport_flash_running_ = false;
                return;
            }
            const int layer = std::min(16, std::max(1, config_.warning_flash_cc_layer));
            const int masks[8] = {0b0001, 0b0011, 0b0111, 0b1111, 0b1110, 0b1100, 0b1000, 0b0000};
            for (int b = 1; b <= 4; ++b) {
                osc_handler_->SetUserControlColor(layer, b, color_index);
            }
            // Keep flowing until an explicit stop (or another manual flash request).
            while (manual_transport_flash_running_) {
                for (int i = 0; i < 8 && manual_transport_flash_running_; ++i) {
                    const int mask = masks[i];
                    for (int b = 1; b <= 4; ++b) {
                        const bool on = (mask & (1 << (b - 1))) != 0;
                        osc_handler_->SetUserControlLed(layer, b, on);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(440));
                }
            }
            for (int b = 1; b <= 4; ++b) {
                osc_handler_->SetUserControlLed(layer, b, false);
            }
            manual_transport_flash_running_ = false;
        });
    } catch (const std::exception& e) {
        manual_transport_flash_running_ = false;
        Log("Manual transport flash thread failed: " + std::string(e.what()) + "\n");
    } catch (...) {
        manual_transport_flash_running_ = false;
        Log("Manual transport flash thread failed with unknown error.\n");
    }
}

void ReaperExtension::StopManualTransportFlash() {
    std::unique_ptr<std::thread> thread_to_join;
    {
        std::lock_guard<std::mutex> lock(manual_transport_flash_mutex_);
        manual_transport_flash_running_ = false;
        if (manual_transport_flash_thread_) {
            thread_to_join = std::move(manual_transport_flash_thread_);
        }
    }
    if (thread_to_join && thread_to_join->joinable()) {
        thread_to_join->join();
    }
}

void ReaperExtension::StartMidiCapture() {
    if (midi_capture_running_) {
        return;
    }
    midi_inputs_.clear();
    const int n = GetNumMIDIInputs();
    for (int i = 0; i < n; ++i) {
        char device_name[256];
        if (!GetMIDIInputName(i, device_name, sizeof(device_name))) {
            continue;
        }
        std::string name_lower = device_name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        if (name_lower.find("wing") == std::string::npos) {
            continue;
        }
        midi_Input* in = CreateMIDIInput(i);
        if (in) {
            in->start();
            midi_inputs_.push_back(in);
        }
    }
    if (midi_inputs_.empty()) {
        Log("⚠ No WING MIDI input could be opened for direct capture.\n");
        return;
    }
    midi_capture_running_ = true;
    midi_capture_thread_ = std::make_unique<std::thread>(&ReaperExtension::MidiCaptureLoop, this);
}

void ReaperExtension::StopMidiCapture() {
    if (!midi_capture_running_) {
        return;
    }
    midi_capture_running_ = false;
    if (midi_capture_thread_ && midi_capture_thread_->joinable()) {
        midi_capture_thread_->join();
    }
    midi_capture_thread_.reset();
    for (auto* in : midi_inputs_) {
        if (in) {
            in->stop();
            in->Destroy();
        }
    }
    midi_inputs_.clear();
}

void ReaperExtension::MidiCaptureLoop() {
    while (midi_capture_running_) {
        const auto now_ms = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        for (auto* in : midi_inputs_) {
            if (!in) continue;
            in->SwapBufs(now_ms);
            MIDI_eventlist* list = in->GetReadBuf();
            if (!list) continue;
            int bpos = 0;
            MIDI_event_t* evt = nullptr;
            while ((evt = list->EnumItems(&bpos)) != nullptr) {
                if (evt->size >= 3) {
                    ProcessMidiInput(evt->midi_message, evt->size);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool ReaperExtension::MidiInputHook(bool is_midi, const unsigned char* data, int len, int dev_id) {
    // Get instance
    auto& ext = ReaperExtension::Instance();
    (void)dev_id;
    
    // Only process MIDI messages
    if (!is_midi || len < 3) {
        return false;  // Pass through
    }
    
    if (!ext.midi_actions_enabled_) {
        return false;  // Pass through
    }
    
    ext.ProcessMidiInput(data, len);
    return false;  // Always pass through (don't consume the MIDI)
}

void ReaperExtension::ProcessMidiInput(const unsigned char* data, int len) {
    if (len < 3) return;
    if (suppress_midi_processing_) {
        return;
    }
    
    unsigned char status = data[0];
    unsigned char cc_num = data[1];
    unsigned char value = data[2];
    
    // Check for Control Change on Channel 1 (0xB0)
    if (status != 0xB0) {
        return;  // Not CC on channel 1
    }
    
    // Accept push/toggle value styles; dedupe very fast repeats.
    static auto last_trigger_time = std::chrono::steady_clock::time_point{};
    static int last_trigger_cc = -1;
    const auto now = std::chrono::steady_clock::now();
    const long long now_ms = SteadyNowMs();

    // Ignore all incoming CC while Wing command assignment is being applied/synchronized.
    if (now_ms < suppress_all_cc_until_ms_.load()) {
        return;
    }

    // Ignore short MIDI echo caused by our own play/record highlight updates.
    if ((cc_num == 20 && now_ms < suppress_play_cc_until_ms_.load()) ||
        (cc_num == 21 && now_ms < suppress_record_cc_until_ms_.load())) {
        return;
    }

    if (cc_num == last_trigger_cc &&
        last_trigger_time.time_since_epoch().count() != 0 &&
        (now - last_trigger_time) < std::chrono::milliseconds(80)) {
        return;
    }
    last_trigger_cc = cc_num;
    last_trigger_time = now;
    
    // Map CC numbers to REAPER command IDs
    int command_id = 0;
    
    switch (cc_num) {
        case 20:  // Play
            if (value == 0) {
                return;  // Ignore toggle-off/release messages.
            }
            command_id = 1007;   // Transport: Play
            TriggerManualTransportFlash(6);  // green
            break;
        case 21:  // Record
            if (value == 0) {
                return;  // Ignore toggle-off/release messages.
            }
            command_id = 1013;   // Transport: Record
            manual_record_suppress_until_ms_ = SteadyNowMs() + 2000;
            if (warning_flash_running_) {
                StopWarningFlash(true);
                ClearLayerState();
            }
            TriggerManualTransportFlash(9);  // red
            break;
        case 22:  // Toggle virtual soundcheck mode
            if (value == 0) {
                return;  // Push-button release: ignore to avoid double toggle.
            }
            pending_toggle_soundcheck_mode_ = true;
            break;
        case 23:  // Stop
            if (value == 0) {
                return;  // Push-button release: ignore to avoid duplicate stop command.
            }
            command_id = 40667;  // Transport: Stop (save all recorded media)
            manual_record_suppress_until_ms_ = 0;
            StopManualTransportFlash();
            break;
        case 24:  // Set Marker
            if (value == 0) {
                return;  // Ignore toggle-off/release messages.
            }
            command_id = 40157;  // Markers: Insert marker at current position
            break;
        case 25:  // Previous Marker
            if (value == 0) {
                return;  // Ignore toggle-off/release messages.
            }
            command_id = 40172;  // Markers: Go to previous marker/project start
            break;
        case 26:  // Next Marker
            if (value == 0) {
                return;  // Ignore toggle-off/release messages.
            }
            command_id = 40173;  // Markers: Go to next marker/project end
            break;
        default:
            return;  // Not one of our mapped CCs
    }
    // Execute REAPER command on main thread timer.
    if (command_id != 0) {
        pending_midi_command_ = command_id;
    }
}

}  // namespace WingConnector
