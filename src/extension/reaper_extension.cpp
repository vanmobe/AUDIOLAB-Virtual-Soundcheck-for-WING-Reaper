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
constexpr int kQueryResponseWaitMs = 600;  // Wait time for OSC responses after sending all queries
constexpr int kReaperPlayStatePlayingBit = 1;
constexpr int kReaperPlayStateRecordingBit = 4;

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
            Main_OnCommand(kCmdTransportRecord, 0);  // Transport: Record (main thread)
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
        RouteMainLRToCardForSDRecording();
    }

    // If MIDI actions are enabled in the extension, re-apply current mapping to the Wing.
    SyncMidiActionsToWing();
    
    return true;
}

// Get available channels with sources assigned
std::vector<ChannelSelectionInfo> ReaperExtension::GetAvailableChannels() {
    std::vector<ChannelSelectionInfo> result;
    
    if (!connected_ || !osc_handler_) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Not connected. Cannot query channels.\n");
        return result;
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Querying channels...\n");
    
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
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto use_direct_display_source = [](const std::string& grp) {
        return grp == "A" || grp == "LCL" || grp == "USR";
    };

    // Query source labels used by the selection popup (for name fallback).
    std::set<std::pair<std::string, int>> source_endpoints;
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
        if (ch.stereo_linked) {
            source_endpoints.insert({display_source.first, display_source.second + 1});
        }
    }
    osc_handler_->QueryInputSourceNames(source_endpoints);
    
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

        ChannelSelectionInfo info;
        info.channel_number = ch.channel_number;
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
                src_name = osc_handler_->QueryInputSourceNameDirect(info.source_group, info.source_input);
            }
            if (!src_name.empty()) {
                info.name = src_name;
            }
        }

        info.stereo_linked = is_stereo;
        info.selected = !info.name.empty() && info.name.rfind("CH", 0) != 0 && info.name.rfind("Channel ", 0) != 0;

        result.push_back(info);
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "AUDIOLAB.wing.reaper.virtualsoundcheck: Found %d channels with sources\n", 
             (int)result.size());
    Log(msg);
    
    return result;
}

// Create tracks from selected channels
void ReaperExtension::CreateTracksFromSelection(const std::vector<ChannelSelectionInfo>& channels) {
    if (!connected_ || !osc_handler_) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Not connected\n");
        return;
    }
    
    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Creating tracks from selection...\n");
    
    // Filter to only selected channels
    std::vector<ChannelSelectionInfo> selected;
    for (const auto& ch : channels) {
        if (ch.selected) {
            selected.push_back(ch);
        }
    }
    
    if (selected.empty()) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: No channels selected\n");
        return;
    }
    
    // Get full channel data from OSC handler
    const auto& channel_data = osc_handler_->GetChannelData();
    
    // Build filtered channel data map
    std::map<int, ChannelInfo> filtered_data;
    for (const auto& sel : selected) {
        auto it = channel_data.find(sel.channel_number);
        if (it != channel_data.end()) {
            ChannelInfo ch_info = it->second;
            if (ch_info.name.empty()) {
                if (!sel.name.empty()) {
                    ch_info.name = sel.name;
                } else {
                    ch_info.name = "CH" + std::to_string(sel.channel_number);
                }
            }
            filtered_data[sel.channel_number] = ch_info;
        }
    }
    
    // Create tracks
    int track_count = track_manager_->CreateTracksFromChannelData(filtered_data);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "AUDIOLAB.wing.reaper.virtualsoundcheck: Created %d tracks\n", track_count);
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

// Setup virtual soundcheck from selected channels
void ReaperExtension::SetupSoundcheckFromSelection(const std::vector<ChannelSelectionInfo>& channels, bool setup_soundcheck) {
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
    
    // Filter to only selected channels
    std::vector<ChannelInfo> selected_channels;
    const auto& channel_data = osc_handler_->GetChannelData();
    
    for (const auto& sel : channels) {
        if (sel.selected) {
            auto it = channel_data.find(sel.channel_number);
            if (it != channel_data.end()) {
                ChannelInfo ch_info = it->second;
                if (ch_info.name.empty()) {
                    if (!sel.name.empty()) {
                        ch_info.name = sel.name;
                    } else {
                        ch_info.name = "CH" + std::to_string(sel.channel_number);
                    }
                }
                selected_channels.push_back(ch_info);
            }
        }
    }
    
    if (selected_channels.empty()) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: No channels selected\n");
        return;
    }
    
    // Refresh stereo link status from Wing console BEFORE calculating allocation
    Log("Refreshing channel stereo link status from Wing...\n");
    for (const auto& ch : selected_channels) {
        osc_handler_->QueryChannel(ch.channel_number);
    }
    // Allow time for responses to arrive
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Get updated channel data with fresh stereo_linked status
    selected_channels.clear();
    const auto& updated_channel_data = osc_handler_->GetChannelData();
    for (const auto& sel : channels) {
        if (sel.selected) {
            auto it = updated_channel_data.find(sel.channel_number);
            if (it != updated_channel_data.end()) {
                ChannelInfo ch_info = it->second;
                if (ch_info.name.empty()) {
                    if (!sel.name.empty()) {
                        ch_info.name = sel.name;
                    } else {
                        ch_info.name = "CH" + std::to_string(sel.channel_number);
                    }
                }
                selected_channels.push_back(ch_info);
            }
        }
    }
    
    // Calculate USB allocation
    auto allocations = osc_handler_->CalculateUSBAllocation(selected_channels);
    
    // Get output mode for display
    std::string output_mode = config_.soundcheck_output_mode;
    std::string output_type = (output_mode == "CARD") ? "CARD" : "USB";
    const int wing_bank_limit = (output_mode == "CARD") ? 32 : 48;

    int required_io_channels = 0;
    for (const auto& alloc : allocations) {
        if (!alloc.allocation_note.empty() && alloc.allocation_note.find("ERROR") != std::string::npos) {
            continue;
        }
        if (alloc.usb_end > required_io_channels) {
            required_io_channels = alloc.usb_end;
        }
    }

    if (required_io_channels > wing_bank_limit) {
        std::ostringstream err;
        err << "AUDIOLAB.wing.reaper.virtualsoundcheck: Selected channels exceed the Wing "
            << output_type << " bank. Required by current selection: " << required_io_channels
            << ", available on Wing: " << wing_bank_limit << ".\n";
        Log(err.str());

        std::ostringstream msg;
        msg << "Selected " << output_type << " routing needs " << required_io_channels
            << " channels, but the Wing exposes only " << wing_bank_limit
            << " " << output_type << " slots.\n\n"
            << "The last stereo pair would be incomplete or missing.\n\n"
            << "Choose fewer channels or switch output mode.";
        ShowMessageBox(msg.str().c_str(), "AUDIOLAB.wing.reaper.virtualsoundcheck - Output Bank Exceeded", 0);
        return;
    }

    const int available_inputs = GetNumAudioInputs();
    const int available_outputs = GetNumAudioOutputs();
    if (available_inputs < required_io_channels || available_outputs < required_io_channels) {
        std::ostringstream err;
        err << "AUDIOLAB.wing.reaper.virtualsoundcheck: REAPER audio device does not expose enough channels for "
            << output_type << " soundcheck. Required by current selection: "
            << required_io_channels << " in / " << required_io_channels << " out, available: "
            << available_inputs << " in / " << available_outputs << " out.\n";
        Log(err.str());

        std::ostringstream msg;
        msg << "Selected " << output_type << " routing requires at least "
            << required_io_channels << " REAPER inputs and outputs.\n\n"
            << "Available now: " << available_inputs << " inputs / "
            << available_outputs << " outputs.\n\n"
            << "Please switch REAPER audio device/range or choose fewer channels.";
        ShowMessageBox(msg.str().c_str(), "AUDIOLAB.wing.reaper.virtualsoundcheck - Audio I/O Not Available", 0);
        return;
    }
    
    // Show what will be configured
    Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Log("CONFIGURING WING CONSOLE FOR VIRTUAL SOUNDCHECK\n");
    Log("Output Mode: " + output_type + "\n");
    Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Log("\nChannels to configure:\n");
    for (const auto& alloc : allocations) {
        std::string line = "  CH" + std::to_string(alloc.channel_number);
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
    if (setup_soundcheck) {
        Log("Step 1/2: Configuring Wing " + output_type + " outputs and ALT sources...\n");
    } else {
        Log("Step 1/2: Configuring Wing " + output_type + " outputs (recording only, no soundcheck)...\n");
    }
    // Query USR routing data before configuring (to resolve routing chains)
    osc_handler_->QueryUserSignalInputs(48);
    osc_handler_->ApplyUSBAllocationAsAlt(allocations, selected_channels, output_mode, setup_soundcheck);
    
    Log("\nStep 2/2: Creating REAPER tracks...\n");
    
    // Create tracks
    std::map<int, ChannelInfo> channel_map;
    for (const auto& ch : selected_channels) {
        channel_map[ch.channel_number] = ch;
    }
    
    Undo_BeginBlock();
    
    int track_index = 0;
    int created_tracks = 0;
    for (const auto& alloc : allocations) {
        if (!alloc.allocation_note.empty() && alloc.allocation_note.find("ERROR") != std::string::npos) {
            continue;
        }
        
        auto it = channel_map.find(alloc.channel_number);
        if (it == channel_map.end()) {
            continue;
        }
        const ChannelInfo& ch_info = it->second;
        const std::string track_name = ch_info.name.empty() ?
            ("CH" + std::to_string(alloc.channel_number)) : ch_info.name;
        
        MediaTrack* track = nullptr;
        if (alloc.is_stereo) {
            track = track_manager_->CreateStereoTrack(track_index, track_name, ch_info.color);
            if (track) {
                track_manager_->SetTrackInput(track, alloc.usb_start - 1, 2);
                track_manager_->SetTrackHardwareOutput(track, alloc.usb_start - 1, 2);
                SetMediaTrackInfo_Value(track, "B_MAINSEND", 0);
                std::string msg = "  ✓ Track " + std::to_string(created_tracks + 1) + 
                                 ": " + track_name + " (stereo) IN " + output_type + " " +
                                 std::to_string(alloc.usb_start) + "-" + std::to_string(alloc.usb_end) +
                                 " / OUT " + output_type + " " +
                                 std::to_string(alloc.usb_start) + "-" + std::to_string(alloc.usb_end) + "\n";
                Log(msg.c_str());
            }
        } else {
            track = track_manager_->CreateTrack(track_index, track_name, ch_info.color);
            if (track) {
                track_manager_->SetTrackInput(track, alloc.usb_start - 1, 1);
                track_manager_->SetTrackHardwareOutput(track, alloc.usb_start - 1, 1);
                SetMediaTrackInfo_Value(track, "B_MAINSEND", 0);
                std::string msg = "  ✓ Track " + std::to_string(created_tracks + 1) + 
                                 ": " + track_name + " (mono) IN " + output_type + " " +
                                 std::to_string(alloc.usb_start) + " / OUT " + output_type + " " +
                                 std::to_string(alloc.usb_start) + "\n";
                Log(msg.c_str());
            }
        }
        
        if (track) {
            track_index++;
            created_tracks++;
        }
    }
    
    Undo_EndBlock("AUDIOLAB.wing.reaper.virtualsoundcheck: Configure Virtual Soundcheck", UNDO_STATE_TRACKCFG);
    
    Log("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Log("✓ CONFIGURATION COMPLETE\n");
    Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    std::string final_msg = "Created " + std::to_string(created_tracks) + " REAPER tracks\n";
    Log(final_msg.c_str());
    Log("\nUse 'Toggle Soundcheck Mode' to enable/disable ALT sources.\n");
    Log("When enabled, channels receive audio from REAPER via USB.\n\n");
}

void ReaperExtension::DisconnectFromWing() {
    if (!connected_) {
        return;
    }
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
                        if (config_.sd_auto_record_with_reaper && osc_handler_) {
                            ApplySDRoutingNoDialog();
                            osc_handler_->StartSDRecorder();
                        }
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
            if (osc_handler_) {
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
                    if (config_.sd_auto_record_with_reaper && osc_handler_) {
                        osc_handler_->StopSDRecorder();
                    }
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
    if (!osc_handler_) {
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
    if (!osc_handler_) {
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
    // Top row (bu): play/record as toggle, soundcheck/stop as push
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 1, 1, MIDI_ACTIONS[0].cc_number, 0, false, true);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 2, 1, MIDI_ACTIONS[1].cc_number, 0, false, true);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 3, 1, MIDI_ACTIONS[2].cc_number, 0, false, false);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 4, 1, MIDI_ACTIONS[3].cc_number, 0, false, false);
    // Bottom row (bd): set/prev/next marker on buttons 1..3
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 1, 1, MIDI_ACTIONS[4].cc_number, 0, true, true);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 2, 1, MIDI_ACTIONS[5].cc_number, 0, true, true);
    osc_handler_->SetUserControlButtonMidiCCToggle(layer, 3, 1, MIDI_ACTIONS[6].cc_number, 0, true, true);
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
    if (warning_flash_running_ || !osc_handler_) {
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
    if (!osc_handler_) {
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

    Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Routing Main LR to CARD 1/2 for SD recording...\n");
    osc_handler_->SetCardOutputSource(1, group, left_input);
    osc_handler_->SetCardOutputSource(2, group, right_input);
    osc_handler_->SetCardOutputName(1, "Main L");
    osc_handler_->SetCardOutputName(2, "Main R");

    const std::string msg = "Configured CARD outputs: 1=" + group + ":" + std::to_string(left_input) +
                            ", 2=" + group + ":" + std::to_string(right_input) + "\n";
    Log(msg);
    ShowMessageBox(
        "Configured CARD 1/2 from Main LR source.\nUse Wing SD recorder to start capture.",
        "AUDIOLAB.wing.reaper.virtualsoundcheck",
        0
    );
}

void ReaperExtension::ApplySDRoutingNoDialog() {
    if (!osc_handler_) {
        return;
    }
    const int left_input = std::max(1, config_.sd_lr_left_input);
    const int right_input = std::max(1, config_.sd_lr_right_input);
    const std::string group = config_.sd_lr_group.empty() ? "MAIN" : config_.sd_lr_group;
    osc_handler_->SetCardOutputSource(1, group, left_input);
    osc_handler_->SetCardOutputSource(2, group, right_input);
    osc_handler_->SetCardOutputName(1, "Main L");
    osc_handler_->SetCardOutputName(2, "Main R");
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
    
    // Get all channel data
    const auto& channels = osc_handler_->GetChannelData();
    if (channels.empty()) {
        ShowMessageBox(
            "No channel data available.\nPlease refresh tracks first.",
            "AUDIOLAB.wing.reaper.virtualsoundcheck - Virtual Soundcheck",
            0
        );
        return;
    }
    
    // Build list of channels with names and sources (these are the "useful" ones)
    std::vector<ChannelInfo> all_channels;
    std::vector<ChannelInfo> selectable_channels;  // Only channels with name AND source
    std::set<int> included_channel_numbers;  // Track which channels are included
    
    for (const auto& pair : channels) {
        all_channels.push_back(pair.second);
        // Only include if has both name and source
        if (!pair.second.name.empty() && !pair.second.primary_source_group.empty()) {
            selectable_channels.push_back(pair.second);
            included_channel_numbers.insert(pair.second.channel_number);
        }
    }
    
    // For stereo-linked channels, ensure BOTH partners are included
    // even if one doesn't have its own name
    std::vector<ChannelInfo> additional_channels;
    for (const auto& ch : selectable_channels) {
        if (ch.stereo_linked) {
            int partner_num = -1;
            
            if (ch.channel_number % 2 == 1) {
                // Odd channel: partner is next even channel
                partner_num = ch.channel_number + 1;
            } else {
                // Even channel: partner is previous odd channel
                partner_num = ch.channel_number - 1;
            }
            
            // Check if partner exists and isn't already included
            if (included_channel_numbers.find(partner_num) == included_channel_numbers.end()) {
                // Find the partner in all_channels
                for (const auto& pair : channels) {
                    if (pair.second.channel_number == partner_num) {
                        additional_channels.push_back(pair.second);
                        included_channel_numbers.insert(partner_num);
                        ShowConsoleMsg("AUDIOLAB.wing.reaper.virtualsoundcheck: Auto-including stereo partner CH");
                        char buf[10];
                        snprintf(buf, sizeof(buf), "%d", partner_num);
                        ShowConsoleMsg(buf);
                        ShowConsoleMsg(" for CH");
                        snprintf(buf, sizeof(buf), "%d", ch.channel_number);
                        ShowConsoleMsg(buf);
                        ShowConsoleMsg("\n");
                        break;
                    }
                }
            }
        }
    }
    
    // Add stereo partners to selectable list
    selectable_channels.insert(selectable_channels.end(), additional_channels.begin(), additional_channels.end());
    
    if (selectable_channels.empty()) {
        ShowMessageBox(
            "No channels with both name and source found.\n\n"
            "To proceed:\n"
            "1. Assign names to Wing channels\n"
            "2. Assign input sources to Wing channels\n"
            "3. Refresh tracks and try again",
            "AUDIOLAB.wing.reaper.virtualsoundcheck - No Channels Available",
            0
        );
        return;
    }
    
    // Build display of available channels
    std::ostringstream channel_list;
    channel_list << "Available channels:\n\n";
    
    for (const auto& ch : selectable_channels) {
        channel_list << "  CH" << std::setw(2) << std::setfill('0') << ch.channel_number 
                    << ": " << std::setfill(' ') << std::left << std::setw(25) << ch.name 
                    << " (" << ch.primary_source_group 
                    << std::to_string(ch.primary_source_input) << ")\n";
    }
    
    channel_list << "\nTotal: " << selectable_channels.size() << " channels\n"
                << "\nCustomize channel selection?";
    
    int result = ShowMessageBox(
        channel_list.str().c_str(),
        "AUDIOLAB.wing.reaper.virtualsoundcheck - Channel Selection",
        4  // Yes/No/Cancel
    );
    
    if (result == 0) {
        Log("AUDIOLAB.wing.reaper.virtualsoundcheck: Virtual Soundcheck configuration cancelled\n");
        return;
    }
    
    std::vector<ChannelInfo> selected_channels = selectable_channels;
    
    // Apply include/exclude filtering from config
    if (!config_.include_channels.empty() || !config_.exclude_channels.empty()) {
        std::vector<ChannelInfo> filtered_channels;
        
        // Parse include and exclude lists
        std::set<int> included_set = ParseChannelSelection(config_.include_channels, 48);
        std::set<int> excluded_set = ParseChannelSelection(config_.exclude_channels, 48);
        
        for (const auto& ch : selected_channels) {
            bool should_include = true;
            
            // If include list exists, only include channels in the list
            if (!included_set.empty()) {
                should_include = (included_set.find(ch.channel_number) != included_set.end());
            }
            
            // Exclude any channels in the exclude list
            if (excluded_set.find(ch.channel_number) != excluded_set.end()) {
                should_include = false;
            }
            
            if (should_include) {
                filtered_channels.push_back(ch);
            }
        }
        
        selected_channels = filtered_channels;
        
        if (selected_channels.empty()) {
            Log("AUDIOLAB.wing.reaper.virtualsoundcheck: No channels remain after filtering. Using all channels.\n");
            selected_channels = selectable_channels;
        }
    }
    
    // If user wants to customize (clicked "Yes"=6)
    if (result == 6) {
        Log("\n=== CHANNEL SELECTION ===\n");
        Log("Available channels:\n");
        for (const auto& ch : selectable_channels) {
            Log("  CH");
            char ch_buf[10];
            snprintf(ch_buf, sizeof(ch_buf), "%02d", ch.channel_number);
            Log(ch_buf);
            Log(": ");
            Log(ch.name.c_str());
            Log(" (");
            Log(ch.primary_source_group.c_str());
            char in_buf[10];
            snprintf(in_buf, sizeof(in_buf), "%d", ch.primary_source_input);
            Log(in_buf);
            Log(")\n");
        }
        
        Log("\nTo proceed with only specific channels, use the following format:\n");
        Log("  INCLUDE: 1,3-5,7 (or leave blank for all)\n");
        Log("  EXCLUDE: 2,4,6   (or leave blank to exclude none)\n");
        Log("\nPlease edit the config.json file and set:\n");
        Log("  \"include_channels\": \"...\"\n");
        Log("  \"exclude_channels\": \"...\"\n");
        Log("\nThen restart the virtual soundcheck process.\n\n");
        
        // For now, use all channels if user chose customize
        // (they can edit config and re-run)
        Log("Proceeding with all available channels.\n");
        Log("(Edit config.json to customize channel selection)\n\n");
    }
    
    // Calculate USB allocation with gap backfilling for selected channels
    auto allocations = osc_handler_->CalculateUSBAllocation(selected_channels);
    const int wing_bank_limit = 48;
    int required_io_channels = 0;
    for (const auto& alloc : allocations) {
        if (!alloc.allocation_note.empty() && alloc.allocation_note.find("ERROR") != std::string::npos) {
            continue;
        }
        if (alloc.usb_end > required_io_channels) {
            required_io_channels = alloc.usb_end;
        }
    }
    if (required_io_channels > wing_bank_limit) {
        std::ostringstream msg;
        msg << "Selected USB routing needs " << required_io_channels
            << " channels, but the Wing exposes only " << wing_bank_limit
            << " USB slots.";
        Log(msg.str() + "\n");
        ShowMessageBox(msg.str().c_str(), "AUDIOLAB.wing.reaper.virtualsoundcheck - Output Bank Exceeded", 0);
        return;
    }
    
    // Show what will be configured
    Log("Channels to configure:\n");
    for (const auto& alloc : allocations) {
        std::string line = "  CH" + std::to_string(alloc.channel_number);
        if (alloc.is_stereo) {
            line += " (stereo) → USB " + std::to_string(alloc.usb_start) + "-" + std::to_string(alloc.usb_end);
        } else {
            line += " (mono) → USB " + std::to_string(alloc.usb_start);
        }
        line += "\n";
        Log(line.c_str());
    }
    Log("\n");
    
    // Proceed directly to configuration (no confirmation dialog)
    // Create a progress message
    Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Log("CONFIGURING WING CONSOLE FOR VIRTUAL SOUNDCHECK\n");
    Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Log("\n");
    
    // Apply the USB allocation: configure USB outputs (Wing -> REAPER) and ALT sources (REAPER -> Wing)
    Log("Step 1/2: Configuring Wing USB outputs and ALT sources...\n");
    // Query USR routing data before configuring USB (to resolve routing chains)
    osc_handler_->QueryUserSignalInputs(48);
    osc_handler_->ApplyUSBAllocationAsAlt(allocations, selected_channels);
    
    Log("\n");
    Log("Step 2/2: Creating REAPER tracks...\n");
    
    // Create REAPER tracks for recording/playback
    // Build lookup map from channel number to channel info
    std::map<int, ChannelInfo> channel_map;
    for (const auto& ch : selected_channels) {
        channel_map[ch.channel_number] = ch;
    }
    
    // Clear existing tracks and create new ones
    Undo_BeginBlock();
    
    int track_index = 0;
    int created_tracks = 0;
    for (const auto& alloc : allocations) {
        if (!alloc.allocation_note.empty() && alloc.allocation_note.find("ERROR") != std::string::npos) {
            continue;  // Skip errored allocations
        }
        
        // Find channel info
        auto it = channel_map.find(alloc.channel_number);
        if (it == channel_map.end()) {
            continue;
        }
        const ChannelInfo& ch_info = it->second;
        
        // Create track (mono or stereo)
        MediaTrack* track = nullptr;
        if (alloc.is_stereo) {
            track = track_manager_->CreateStereoTrack(track_index, ch_info.name, ch_info.color);
            // Set stereo USB input (USB channels are 1-based, REAPER inputs are 0-based)
            if (track) {
                track_manager_->SetTrackInput(track, alloc.usb_start - 1, 2);
                std::string msg = "  ✓ Track " + std::to_string(created_tracks + 1) + 
                                 ": " + ch_info.name + " (stereo) → USB " + 
                                 std::to_string(alloc.usb_start) + "-" + std::to_string(alloc.usb_end) + "\n";
                Log(msg.c_str());
            }
        } else {
            track = track_manager_->CreateTrack(track_index, ch_info.name, ch_info.color);
            // Set mono USB input
            if (track) {
                track_manager_->SetTrackInput(track, alloc.usb_start - 1, 1);
                std::string msg = "  ✓ Track " + std::to_string(created_tracks + 1) + 
                                 ": " + ch_info.name + " (mono) → USB " + 
                                 std::to_string(alloc.usb_start) + "\n";
                Log(msg.c_str());
            }
        }
        
        if (track) {
            track_index++;
            created_tracks++;
        }
    }
    
    Undo_EndBlock("AUDIOLAB.wing.reaper.virtualsoundcheck: Configure Virtual Soundcheck", UNDO_STATE_TRACKCFG);
        
        Log("\n");
        Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        Log("✓ CONFIGURATION COMPLETE\n");
        Log("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        std::string final_msg = "Created " + std::to_string(created_tracks) + " REAPER tracks\n";
        Log(final_msg.c_str());
        Log("\nNext: Use 'Toggle Soundcheck Mode' to enable/disable ALT sources.\n");
        Log("When enabled, channels receive audio from REAPER via USB.\n");
        Log("\n");
        
        // Build success message
        std::ostringstream success_msg;
        success_msg << "Virtual Soundcheck configured successfully!\n\n"
                    << "Created " << created_tracks << " REAPER tracks.\n\n"
                    << "Use 'Toggle Soundcheck Mode' to enable/disable ALT sources.\n"
                    << "When enabled, channels receive audio from REAPER via USB.\n\n"
                    << "Details are shown in the REAPER console.";
        
        ShowMessageBox(
            success_msg.str().c_str(),
            "AUDIOLAB.wing.reaper.virtualsoundcheck - Success",
            0
        );
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
        midi_actions_enabled_ = false;
        StopManualTransportFlash();
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
    if (!connected_ || !osc_handler_) {
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
