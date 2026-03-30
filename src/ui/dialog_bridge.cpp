/*
 * Cross-platform dialog bridge.
 */

#include <cstring>

#include "internal/adoption_plan.h"
#include "internal/dialog_bridge.h"
#include "wingconnector/reaper_extension.h"
#ifdef __APPLE__
#include "internal/wing_connector_dialog_macos.h"
#endif
#include "reaper_plugin_functions.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <set>
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

std::string NormalizeToken(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            normalized.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return normalized;
}

bool LooksLikeStereoName(const std::string& name) {
    const std::string normalized = NormalizeToken(name);
    return normalized.find("stereo") != std::string::npos ||
           normalized.find("pair") != std::string::npos ||
           normalized.find("lr") != std::string::npos;
}

bool TrackHasStereoMedia(MediaTrack* track) {
    if (!track) {
        return false;
    }

    const int item_count = CountTrackMediaItems(track);
    for (int item_index = 0; item_index < item_count; ++item_index) {
        MediaItem* item = GetTrackMediaItem(track, item_index);
        if (!item) {
            continue;
        }

        MediaItem_Take* take = GetActiveTake(item);
        if (!take) {
            take = GetMediaItemTake(item, 0);
        }
        if (!take) {
            continue;
        }

        PCM_source* source = GetMediaItemTake_Source(take);
        if (!source) {
            continue;
        }

        if (GetMediaSourceNumChannels(source) >= 2) {
            return true;
        }
    }

    return false;
}

struct AdoptionTrackInfo {
    int track_index = 0;  // 1-based
    std::string name;
    bool managed = false;
    bool adopted_in_place = false;
    bool has_items = false;
    bool stereo_like = false;
    std::string source_id;
    int managed_channel_number = 0;
    int current_slot_start = 0;
    int current_slot_end = 0;
};

struct AdoptionSuggestion {
    AdoptionTrackInfo track;
    SourceSelectionInfo source;
    std::string reason;
};

struct AdoptionDialogPlanRow {
    AdoptionSuggestion suggestion;
    WingConnector::AdoptionPlan::Row plan_row;
};

struct AdoptionDialogPlan {
    std::string output_mode = "USB";
    std::vector<AdoptionDialogPlanRow> rows;
};

struct ExistingManagedRoute {
    int track_index = 0;
    std::string source_id;
    int slot_start = 0;
    int slot_end = 0;
};

std::string NormalizeUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string NormalizeOutputMode(std::string value) {
    return AdoptionPlan::NormalizeOutputMode(NormalizeUpper(TrimCopy(value)));
}

bool ParseTrackKey(const std::string& token, int& track_index_out) {
    std::string upper = NormalizeUpper(TrimCopy(token));
    if (upper.rfind("TRACK", 0) == 0) {
        upper.erase(0, 5);
    } else if (upper.rfind("TR", 0) == 0) {
        upper.erase(0, 2);
    } else if (upper.rfind("T", 0) == 0) {
        upper.erase(0, 1);
    }
    try {
        track_index_out = std::stoi(upper);
        return track_index_out > 0;
    } catch (...) {
        return false;
    }
}

bool DecodeTrackInput(MediaTrack* track, int& slot_start_out, int& slot_end_out) {
    if (!track) {
        return false;
    }
    const int rec_input = static_cast<int>(GetMediaTrackInfo_Value(track, "I_RECINPUT"));
    if (rec_input < 0) {
        return false;
    }
    const int input_index = rec_input & 0x3FF;
    const bool stereo = ((rec_input >> 10) & 0x1) != 0;
    slot_start_out = input_index + 1;
    slot_end_out = slot_start_out + (stereo ? 1 : 0);
    return true;
}

std::vector<ExistingManagedRoute> CollectExistingManagedRoutes() {
    std::vector<ExistingManagedRoute> routes;
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    if (!proj) {
        return routes;
    }

    const int track_count = CountTracks(proj);
    for (int i = 0; i < track_count; ++i) {
        MediaTrack* track = GetTrack(proj, i);
        if (!track) {
            continue;
        }

        char source_id_buf[256]{};
        if (!GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_SOURCE_ID", source_id_buf, false) ||
            source_id_buf[0] == '\0') {
            continue;
        }

        ExistingManagedRoute route;
        route.track_index = i + 1;
        route.source_id = source_id_buf;
        if (!DecodeTrackInput(track, route.slot_start, route.slot_end)) {
            continue;
        }
        routes.push_back(route);
    }
    return routes;
}

std::set<int> CollectManagedChannelNumbers() {
    std::set<int> managed_channels;
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    if (!proj) {
        return managed_channels;
    }

    const int track_count = CountTracks(proj);
    for (int i = 0; i < track_count; ++i) {
        MediaTrack* track = GetTrack(proj, i);
        if (!track) {
            continue;
        }

        char source_id_buf[256]{};
        if (!GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_SOURCE_ID", source_id_buf, false) ||
            source_id_buf[0] == '\0') {
            continue;
        }

        const std::string source_id = source_id_buf;
        if (source_id.rfind("CH:", 0) != 0) {
            continue;
        }
        try {
            managed_channels.insert(std::stoi(source_id.substr(3)));
        } catch (...) {
        }
    }

    return managed_channels;
}

bool ParseManagedChannelSourceId(const std::string& source_id, int& channel_number_out) {
    if (source_id.rfind("CH:", 0) != 0) {
        return false;
    }
    try {
        channel_number_out = std::stoi(source_id.substr(3));
        return channel_number_out > 0;
    } catch (...) {
        return false;
    }
}

std::vector<AdoptionTrackInfo> ScanProjectTracks() {
    std::vector<AdoptionTrackInfo> tracks;
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    if (!proj) {
        return tracks;
    }

    const int track_count = CountTracks(proj);
    tracks.reserve(track_count);
    for (int i = 0; i < track_count; ++i) {
        MediaTrack* track = GetTrack(proj, i);
        if (!track) {
            continue;
        }

        AdoptionTrackInfo info;
        info.track_index = i + 1;
        char name_buf[512]{};
        if (GetSetMediaTrackInfo_String(track, "P_NAME", name_buf, false) && name_buf[0] != '\0') {
            info.name = name_buf;
        } else {
            info.name = "Track " + std::to_string(i + 1);
        }

        char source_id_buf[256]{};
        info.managed = GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_SOURCE_ID", source_id_buf, false) &&
                       source_id_buf[0] != '\0';
        if (info.managed) {
            info.source_id = source_id_buf;
            info.adopted_in_place =
                GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_ADOPTED_IN_PLACE", source_id_buf, false) &&
                source_id_buf[0] == '1';
            ParseManagedChannelSourceId(info.source_id, info.managed_channel_number);
        }
        info.has_items = CountTrackMediaItems(track) > 0;
        info.stereo_like = TrackHasStereoMedia(track) || LooksLikeStereoName(info.name);
        DecodeTrackInput(track, info.current_slot_start, info.current_slot_end);
        tracks.push_back(info);
    }

    return tracks;
}

std::vector<SourceSelectionInfo> FilterChannelSources(const std::vector<SourceSelectionInfo>& sources) {
    std::vector<SourceSelectionInfo> channels;
    const auto tracks = ScanProjectTracks();
    std::set<int> reusable_adopted_channels;
    for (const auto& track : tracks) {
        if (track.adopted_in_place && track.managed_channel_number > 0) {
            reusable_adopted_channels.insert(track.managed_channel_number);
        }
    }
    const std::set<int> managed_channels = CollectManagedChannelNumbers();
    for (const auto& source : sources) {
        if (source.kind == SourceKind::Channel &&
            (managed_channels.count(source.source_number) == 0 ||
             reusable_adopted_channels.count(source.source_number) > 0)) {
            channels.push_back(source);
        }
    }
    return channels;
}

bool CanAssignTrackToSource(const AdoptionTrackInfo& track,
                            const SourceSelectionInfo& source,
                            const std::set<int>& blocked_channels) {
    (void)track;
    if (blocked_channels.count(source.source_number) > 0) {
        return false;
    }
    return true;
}

void ReserveAssignedChannels(const AdoptionTrackInfo& track,
                             const SourceSelectionInfo& source,
                             std::set<int>& blocked_channels) {
    (void)track;
    blocked_channels.insert(source.source_number);
}

int MatchScore(const AdoptionTrackInfo& track, const SourceSelectionInfo& source) {
    const std::string track_name = NormalizeToken(track.name);
    const std::string source_name = NormalizeToken(source.name);
    const std::string channel_token = "ch" + std::to_string(source.source_number);

    int score = 0;
    if (source.kind == SourceKind::Channel && track.track_index == source.source_number) {
        score += 80;
    }
    if (!track_name.empty() && !source_name.empty()) {
        if (track_name == source_name) {
            score += 100;
        } else if (track_name.find(source_name) != std::string::npos || source_name.find(track_name) != std::string::npos) {
            score += 55;
        }
    }
    if (track_name.find(channel_token) != std::string::npos) {
        score += 40;
    }
    if (track.has_items) {
        score += 5;
    }
    return score;
}

std::vector<AdoptionSuggestion> BuildAdoptionSuggestions(const std::vector<AdoptionTrackInfo>& tracks,
                                                         const std::vector<SourceSelectionInfo>& channel_sources) {
    std::vector<AdoptionSuggestion> suggestions;
    std::set<int> blocked_channels;
    for (const auto& track : tracks) {
        if (!track.managed || track.adopted_in_place) {
            continue;
        }
        if (track.managed_channel_number > 0) {
            blocked_channels.insert(track.managed_channel_number);
        }
    }
    std::set<int> matched_tracks;

    for (const auto& track : tracks) {
        if ((track.managed && !track.adopted_in_place) || !track.has_items) {
            continue;
        }

        int best_score = 0;
        const SourceSelectionInfo* best_source = nullptr;
        for (const auto& source : channel_sources) {
            if (!CanAssignTrackToSource(track, source, blocked_channels)) {
                continue;
            }
            const int score = MatchScore(track, source);
            if (score > best_score) {
                best_score = score;
                best_source = &source;
            }
        }

        if (!best_source || best_score < 40) {
            continue;
        }

        ReserveAssignedChannels(track, *best_source, blocked_channels);
        matched_tracks.insert(track.track_index);
        AdoptionSuggestion suggestion;
        suggestion.track = track;
        suggestion.source = *best_source;
        if (NormalizeToken(track.name) == NormalizeToken(best_source->name)) {
            suggestion.reason = "name match";
        } else if (NormalizeToken(track.name).find("ch" + std::to_string(best_source->source_number)) != std::string::npos) {
            suggestion.reason = "channel-number match";
        } else {
            suggestion.reason = "loose name match";
        }
        suggestions.push_back(suggestion);
    }

    size_t channel_index = 0;
    for (const auto& track : tracks) {
        if ((track.managed && !track.adopted_in_place) || !track.has_items || matched_tracks.count(track.track_index) > 0) {
            continue;
        }

        while (channel_index < channel_sources.size() &&
               !CanAssignTrackToSource(track, channel_sources[channel_index], blocked_channels)) {
            ++channel_index;
        }
        if (channel_index >= channel_sources.size()) {
            break;
        }

        AdoptionSuggestion suggestion;
        suggestion.track = track;
        suggestion.source = channel_sources[channel_index];
        suggestion.reason = "sequential fallback";
        suggestions.push_back(suggestion);
        ReserveAssignedChannels(track, channel_sources[channel_index], blocked_channels);
        ++channel_index;
    }

    std::stable_sort(suggestions.begin(), suggestions.end(), [](const AdoptionSuggestion& lhs,
                                                                const AdoptionSuggestion& rhs) {
        return lhs.track.track_index < rhs.track.track_index;
    });

    return suggestions;
}

std::string BuildAdoptionReviewSummary(const std::vector<AdoptionTrackInfo>& tracks,
                                       const std::vector<SourceSelectionInfo>& channel_sources,
                                       const std::vector<AdoptionSuggestion>& suggestions) {
    int managed_tracks = 0;
    int import_like_tracks = 0;
    int ignored_tracks = 0;
    for (const auto& track : tracks) {
        if (track.managed) {
            managed_tracks++;
        } else if (track.has_items) {
            import_like_tracks++;
        } else {
            ignored_tracks++;
        }
    }

    std::ostringstream out;
    out << "Existing Project Adoption Review\n\n"
        << "This is a review-first flow. WINGuard will not write any track metadata or routing unless you confirm Adopt In Place after this summary.\n\n"
        << "Project scan:\n"
        << "- Total REAPER tracks: " << tracks.size() << "\n"
        << "- Already managed: " << managed_tracks << "\n"
        << "- Candidate imported tracks with media: " << import_like_tracks << "\n"
        << "- Ignored for now (no media items or managed already): " << ignored_tracks << "\n\n"
        << "WING scan:\n"
        << "- Available channel sources: " << channel_sources.size() << "\n\n"
        << "Suggested channel mappings: " << suggestions.size() << "\n";

    const size_t preview_count = std::min<size_t>(6, suggestions.size());
    for (size_t i = 0; i < preview_count; ++i) {
        const auto& suggestion = suggestions[i];
        out << "- Track " << suggestion.track.track_index << " (" << suggestion.track.name << ") -> "
            << "CH" << suggestion.source.source_number;
        if (!suggestion.source.name.empty()) {
            out << " (" << suggestion.source.name << ")";
        }
        out << " [" << suggestion.reason << "]\n";
    }
    if (suggestions.size() > preview_count) {
        out << "- ...\n";
    }

    out << "\nPlanned v1 behavior:\n"
        << "- separate adoption action\n"
        << "- review mappings before any write\n"
        << "- Adopt In Place marks the matched imported tracks as managed and rewrites their WINGuard routing\n"
        << "- channels only in v1\n"
        << "- preserve imported track names, order, and FX\n";

    if (suggestions.empty()) {
        out << "\nNo channel mappings were suggested from the current project. This usually means there were no remaining unmanaged WING channels or no imported tracks with media items to adopt.\n";
    }

    return out.str();
}

AdoptionDialogPlan BuildDefaultAdoptionPlan(const std::vector<AdoptionSuggestion>& suggestions, const std::string& output_mode) {
    AdoptionDialogPlan plan;
    plan.output_mode = NormalizeOutputMode(output_mode);
    plan.rows.reserve(suggestions.size());
    for (const auto& suggestion : suggestions) {
        AdoptionDialogPlanRow row;
        row.suggestion = suggestion;
        row.plan_row.track.track_index = suggestion.track.track_index;
        row.plan_row.track.name = suggestion.track.name;
        row.plan_row.track.stereo_like = suggestion.track.stereo_like;
        row.plan_row.assigned_source = suggestion.source;
        row.plan_row.assigned_source.selected = true;
        row.plan_row.assigned_source.stereo_linked = suggestion.track.stereo_like;
        row.plan_row.assigned_source.stereo_intent_override = suggestion.track.stereo_like;
        plan.rows.push_back(row);
    }
    return plan;
}

bool ParseAssignedChannelValue(const std::string& token, int& channel_number_out) {
    std::string upper = NormalizeUpper(TrimCopy(token));
    if (upper.rfind("CH", 0) == 0) {
        upper.erase(0, 2);
    }
    try {
        channel_number_out = std::stoi(upper);
        return channel_number_out > 0;
    } catch (...) {
        return false;
    }
}

bool ParseSlotValue(const std::string& token,
                    const std::string& output_mode,
                    bool stereo,
                    int& slot_start_out,
                    int& slot_end_out,
                    std::string& error_out) {
    std::string upper = NormalizeUpper(TrimCopy(token));
    if (upper.empty()) {
        error_out = "Playback slot override cannot be empty.";
        return false;
    }
    const std::string mode = NormalizeOutputMode(output_mode);
    if (upper.rfind("USB", 0) == 0) {
        if (mode != "USB") {
            error_out = "Playback slot override uses USB while the plan mode is CARD.";
            return false;
        }
        upper.erase(0, 3);
    } else if (upper.rfind("CARD", 0) == 0) {
        if (mode != "CARD") {
            error_out = "Playback slot override uses CARD while the plan mode is USB.";
            return false;
        }
        upper.erase(0, 4);
    }

    upper = TrimCopy(upper);
    const size_t dash = upper.find('-');
    try {
        if (dash == std::string::npos) {
            slot_start_out = std::stoi(upper);
            slot_end_out = stereo ? (slot_start_out + 1) : slot_start_out;
        } else {
            slot_start_out = std::stoi(upper.substr(0, dash));
            slot_end_out = std::stoi(upper.substr(dash + 1));
        }
    } catch (...) {
        error_out = "Playback slot overrides must look like 9, 9-10, USB9, or CARD9-10.";
        return false;
    }

    if (slot_start_out <= 0 || slot_end_out < slot_start_out) {
        error_out = "Playback slot override range is invalid.";
        return false;
    }
    if (stereo) {
        if (slot_end_out != slot_start_out + 1) {
            error_out = "Stereo rows must use a contiguous slot pair such as 9-10.";
            return false;
        }
        if ((slot_start_out % 2) == 0) {
            error_out = "Stereo rows must start on an odd playback slot.";
            return false;
        }
    } else if (slot_end_out != slot_start_out) {
        error_out = "Mono rows can only use a single playback slot.";
        return false;
    }
    return true;
}

bool ParseAdoptionChannelAssignments(const std::string& spec,
                                     const std::map<int, SourceSelectionInfo>& available_channels,
                                     AdoptionDialogPlan& plan,
                                     std::string& error_out) {
    if (TrimCopy(spec).empty()) {
        return true;
    }

    std::map<int, size_t> row_by_track;
    for (size_t i = 0; i < plan.rows.size(); ++i) {
        row_by_track[plan.rows[i].suggestion.track.track_index] = i;
    }

    for (const auto& token : SplitDelimited(spec, ';')) {
        if (token.empty()) {
            continue;
        }
        const size_t equals_pos = token.find('=');
        if (equals_pos == std::string::npos) {
            error_out = "Channel assignments must look like 2=CH8 or TRACK4=12.";
            return false;
        }

        int track_index = 0;
        if (!ParseTrackKey(token.substr(0, equals_pos), track_index)) {
            error_out = "Channel assignment keys must reference a REAPER track index.";
            return false;
        }
        auto row_it = row_by_track.find(track_index);
        if (row_it == row_by_track.end()) {
            error_out = "Channel assignment references a track that is not part of this adoption plan.";
            return false;
        }

        int channel_number = 0;
        if (!ParseAssignedChannelValue(token.substr(equals_pos + 1), channel_number)) {
            error_out = "Channel assignment targets must look like CH8 or 8.";
            return false;
        }
        auto channel_it = available_channels.find(channel_number);
        if (channel_it == available_channels.end()) {
            error_out = "Requested WING channel CH" + std::to_string(channel_number) +
                        " is not available for adoption in this project.";
            return false;
        }

        auto& row = plan.rows[row_it->second];
        row.plan_row.assigned_source = channel_it->second;
        row.plan_row.assigned_source.selected = true;
        row.plan_row.assigned_source.stereo_linked = row.suggestion.track.stereo_like;
        row.plan_row.assigned_source.stereo_intent_override = row.suggestion.track.stereo_like;
    }
    return true;
}

bool ParseAdoptionSlotOverrides(const std::string& spec,
                                AdoptionDialogPlan& plan,
                                std::string& error_out) {
    if (TrimCopy(spec).empty()) {
        return true;
    }

    std::map<int, size_t> row_by_track;
    for (size_t i = 0; i < plan.rows.size(); ++i) {
        row_by_track[plan.rows[i].suggestion.track.track_index] = i;
    }

    for (const auto& token : SplitDelimited(spec, ';')) {
        if (token.empty()) {
            continue;
        }
        const size_t equals_pos = token.find('=');
        if (equals_pos == std::string::npos) {
            error_out = "Playback slot overrides must look like 2=9-10 or TRACK3=4.";
            return false;
        }

        int track_index = 0;
        if (!ParseTrackKey(token.substr(0, equals_pos), track_index)) {
            error_out = "Playback slot override keys must reference a REAPER track index.";
            return false;
        }
        auto row_it = row_by_track.find(track_index);
        if (row_it == row_by_track.end()) {
            error_out = "Playback slot override references a track that is not part of this adoption plan.";
            return false;
        }

        auto& row = plan.rows[row_it->second];
        int slot_start = 0;
        int slot_end = 0;
        if (!ParseSlotValue(token.substr(equals_pos + 1),
                            plan.output_mode,
                            row.suggestion.track.stereo_like,
                            slot_start,
                            slot_end,
                            error_out)) {
            return false;
        }

        row.plan_row.slot_overridden = true;
        row.plan_row.slot_start = slot_start;
        row.plan_row.slot_end = slot_end;
    }
    return true;
}

bool ValidateAdoptionAssignments(const AdoptionDialogPlan& plan,
                                 const std::vector<ExistingManagedRoute>& existing_routes,
                                 std::string& error_out) {
    std::vector<WingConnector::AdoptionPlan::Row> rows;
    rows.reserve(plan.rows.size());
    for (const auto& row : plan.rows) {
        rows.push_back(row.plan_row);
    }
    std::vector<WingConnector::AdoptionPlan::ExistingRoute> existing;
    existing.reserve(existing_routes.size());
    for (const auto& route : existing_routes) {
        existing.push_back({route.source_id, route.slot_start, route.slot_end});
    }
    return WingConnector::AdoptionPlan::ValidateAssignments(rows, existing, plan.output_mode, error_out);
}

bool BuildAdoptionRequestedAllocations(const AdoptionDialogPlan& plan,
                                       const std::vector<ExistingManagedRoute>& existing_routes,
                                       std::vector<USBAllocation>& allocations_out,
                                       std::string& error_out) {
    std::vector<WingConnector::AdoptionPlan::Row> rows;
    rows.reserve(plan.rows.size());
    for (const auto& row : plan.rows) {
        rows.push_back(row.plan_row);
    }
    std::vector<WingConnector::AdoptionPlan::ExistingRoute> existing;
    existing.reserve(existing_routes.size());
    for (const auto& route : existing_routes) {
        existing.push_back({route.source_id, route.slot_start, route.slot_end});
    }
    return WingConnector::AdoptionPlan::BuildRequestedAllocations(rows, existing, plan.output_mode, allocations_out, error_out);
}

std::vector<SourceSelectionInfo> BuildSelectedSourcesFromPlan(const AdoptionDialogPlan& plan) {
    std::vector<WingConnector::AdoptionPlan::Row> rows;
    rows.reserve(plan.rows.size());
    for (const auto& row : plan.rows) {
        rows.push_back(row.plan_row);
    }
    return WingConnector::AdoptionPlan::BuildSelectedSources(rows);
}

void MarkTracksAdoptedInPlace(const AdoptionDialogPlan& plan) {
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    if (!proj) {
        return;
    }

    for (const auto& row : plan.rows) {
        MediaTrack* track = GetTrack(proj, row.suggestion.track.track_index - 1);
        if (!track) {
            continue;
        }

        const std::string source_id = WingConnector::AdoptionPlan::SourcePersistentId(row.plan_row.assigned_source);
        GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_SOURCE_ID", const_cast<char*>(source_id.c_str()), true);
        GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_ADOPTED_IN_PLACE", const_cast<char*>("1"), true);
    }
}

struct AdoptionTrackMetadataSnapshot {
    int track_index = 0;
    std::string source_id;
    std::string adopted_in_place;
};

std::vector<AdoptionTrackMetadataSnapshot> CaptureAdoptionTrackMetadata(const AdoptionDialogPlan& plan) {
    std::vector<AdoptionTrackMetadataSnapshot> snapshots;
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    if (!proj) {
        return snapshots;
    }

    snapshots.reserve(plan.rows.size());
    for (const auto& row : plan.rows) {
        MediaTrack* track = GetTrack(proj, row.suggestion.track.track_index - 1);
        if (!track) {
            continue;
        }

        AdoptionTrackMetadataSnapshot snapshot;
        snapshot.track_index = row.suggestion.track.track_index;

        char source_id_buf[256]{};
        if (GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_SOURCE_ID", source_id_buf, false) &&
            source_id_buf[0] != '\0') {
            snapshot.source_id = source_id_buf;
        }

        char adopted_buf[32]{};
        if (GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_ADOPTED_IN_PLACE", adopted_buf, false) &&
            adopted_buf[0] != '\0') {
            snapshot.adopted_in_place = adopted_buf;
        }

        snapshots.push_back(snapshot);
    }
    return snapshots;
}

void RestoreAdoptionTrackMetadata(const std::vector<AdoptionTrackMetadataSnapshot>& snapshots) {
    ReaProject* proj = EnumProjects(-1, nullptr, 0);
    if (!proj) {
        return;
    }

    for (const auto& snapshot : snapshots) {
        MediaTrack* track = GetTrack(proj, snapshot.track_index - 1);
        if (!track) {
            continue;
        }

        if (snapshot.source_id.empty()) {
            GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_SOURCE_ID", const_cast<char*>(""), true);
        } else {
            GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_SOURCE_ID", const_cast<char*>(snapshot.source_id.c_str()), true);
        }

        if (snapshot.adopted_in_place.empty()) {
            GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_ADOPTED_IN_PLACE", const_cast<char*>(""), true);
        } else {
            GetSetMediaTrackInfo_String(track, "P_EXT:WINGCONNECTOR_ADOPTED_IN_PLACE", const_cast<char*>(snapshot.adopted_in_place.c_str()), true);
        }
    }
}

bool ParseSimpleOnOffFlag(const std::string& value, bool& enabled_out) {
    std::string upper = TrimCopy(value);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (upper == "1" || upper == "ON" || upper == "YES" || upper == "TRUE") {
        enabled_out = true;
        return true;
    }
    if (upper == "0" || upper == "OFF" || upper == "NO" || upper == "FALSE") {
        enabled_out = false;
        return true;
    }
    return false;
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

#ifndef __APPLE__
const char* SourceKindTag(SourceKind kind) {
    switch (kind) {
        case SourceKind::Channel: return "CH";
        case SourceKind::Bus: return "BUS";
        case SourceKind::Main: return "MAIN";
        case SourceKind::Matrix: return "MTX";
    }
    return "SRC";
}

std::string SourceDisplayName(const SourceSelectionInfo& source) {
    std::ostringstream out;
    out << SourceKindTag(source.kind) << source.source_number;
    if (!source.name.empty()) {
        out << " (" << source.name << ")";
    }
    if (!source.soundcheck_capable) {
        out << " [record only]";
    }
    return out.str();
}

std::string BuildDefaultSelectionSpec(const std::vector<SourceSelectionInfo>& sources) {
    bool any_selected = false;
    bool selected_only_channels = true;
    for (const auto& source : sources) {
        if (!source.selected) {
            continue;
        }
        any_selected = true;
        selected_only_channels = selected_only_channels && source.kind == SourceKind::Channel;
    }
    if (any_selected) {
        return "CURRENT";
    }
    return "ALL_CH";
}

bool ParseSelectionSpec(const std::string& spec,
                        const std::vector<SourceSelectionInfo>& available_sources,
                        std::vector<SourceSelectionInfo>& selected_sources_out,
                        std::string& error_out) {
    selected_sources_out = available_sources;
    for (auto& source : selected_sources_out) {
        source.selected = false;
    }

    const std::string trimmed = TrimCopy(spec);
    if (trimmed.empty()) {
        error_out = "Enter CURRENT, ALL_CH, ALL, or a comma-separated list such as CH1-8,BUS1.";
        return false;
    }

    auto select_if = [&](const std::function<bool(const SourceSelectionInfo&)>& predicate) {
        for (auto& source : selected_sources_out) {
            if (predicate(source)) {
                source.selected = true;
            }
        }
    };

    for (const auto& raw_token : SplitDelimited(trimmed, ',')) {
        std::string token = raw_token;
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (token.empty()) {
            continue;
        }

        if (token == "ALL") {
            select_if([](const SourceSelectionInfo&) { return true; });
            continue;
        }
        if (token == "ALL_CH") {
            select_if([](const SourceSelectionInfo& source) {
                return source.kind == SourceKind::Channel;
            });
            continue;
        }
        if (token == "CURRENT" || token == "MANAGED") {
            select_if([](const SourceSelectionInfo& source) {
                return source.selected;
            });
            continue;
        }
        if (token == "ALL_SC" || token == "ALL_SOUNDCHECK") {
            select_if([](const SourceSelectionInfo& source) {
                return source.soundcheck_capable;
            });
            continue;
        }
        if (token == "NONE") {
            for (auto& source : selected_sources_out) {
                source.selected = false;
            }
            continue;
        }

        size_t prefix_end = 0;
        while (prefix_end < token.size() && !std::isdigit(static_cast<unsigned char>(token[prefix_end]))) {
            ++prefix_end;
        }
        std::string prefix = token.substr(0, prefix_end);
        std::string range = token.substr(prefix_end);
        if (range.empty()) {
            error_out = "Selection token is missing a source number: " + raw_token;
            return false;
        }

        SourceKind kind = SourceKind::Channel;
        if (prefix.empty()) {
            kind = SourceKind::Channel;
        } else if (!ParseSourceKind(prefix, kind)) {
            error_out = "Unknown source family in selection: " + raw_token;
            return false;
        }

        int start = 0;
        int end = 0;
        const size_t dash = range.find('-');
        try {
            if (dash == std::string::npos) {
                start = end = std::stoi(range);
            } else {
                start = std::stoi(range.substr(0, dash));
                end = std::stoi(range.substr(dash + 1));
            }
        } catch (...) {
            error_out = "Selection numbers must be integers: " + raw_token;
            return false;
        }

        if (start > end) {
            std::swap(start, end);
        }
        if (start < 1 || end > MaxSourceNumberForKind(kind)) {
            error_out = "Selection is out of range for " + std::string(SourceKindTag(kind)) + ": " + raw_token;
            return false;
        }

        bool matched = false;
        for (auto& source : selected_sources_out) {
            if (source.kind == kind && source.source_number >= start && source.source_number <= end) {
                source.selected = true;
                matched = true;
            }
        }
        if (!matched) {
            error_out = "Selection did not match any discovered sources: " + raw_token;
            return false;
        }
    }

    const bool any_selected = std::any_of(selected_sources_out.begin(), selected_sources_out.end(), [](const SourceSelectionInfo& source) {
        return source.selected;
    });
    if (!any_selected) {
        error_out = "No sources selected. Choose at least one source before applying setup.";
        return false;
    }

    return true;
}

std::string BuildSourceCatalog(const std::vector<SourceSelectionInfo>& sources) {
    std::ostringstream out;
    out << "Discovered sources:\n";
    for (const auto& source : sources) {
        out << "- " << SourceDisplayName(source) << "\n";
    }
    out << "\nSelection shortcuts:\n"
        << "- CURRENT = rebuild the current managed selection\n"
        << "- ALL_CH = all channels\n"
        << "- ALL = every discovered source\n"
        << "- ALL_SC = all soundcheck-capable sources\n"
        << "- Examples: CH1-8, BUS1-2, MAIN1, MTX1, 1-8\n";
    return out.str();
}

bool ParseOnOffFlag(const std::string& value, bool& enabled_out) {
    std::string upper = TrimCopy(value);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (upper == "1" || upper == "ON" || upper == "YES" || upper == "TRUE") {
        enabled_out = true;
        return true;
    }
    if (upper == "0" || upper == "OFF" || upper == "NO" || upper == "FALSE") {
        enabled_out = false;
        return true;
    }
    return false;
}

std::string BuildSelectionSummary(const std::vector<SourceSelectionInfo>& sources, bool setup_soundcheck, bool replace_existing) {
    int selected_count = 0;
    int soundcheck_count = 0;
    int record_only_count = 0;
    std::vector<std::string> preview;
    for (const auto& source : sources) {
        if (!source.selected) {
            continue;
        }
        ++selected_count;
        if (source.soundcheck_capable) {
            ++soundcheck_count;
        } else {
            ++record_only_count;
        }
        if (preview.size() < 6) {
            preview.push_back(SourceDisplayName(source));
        }
    }

    std::ostringstream out;
    out << "Review setup before applying:\n\n"
        << "Selected sources: " << selected_count << "\n"
        << "Soundcheck-capable: " << soundcheck_count << "\n"
        << "Record-only: " << record_only_count << "\n"
        << "Mode: " << (setup_soundcheck ? "SOUNDCHECK + recording" : "RECORDING only") << "\n"
        << "Track handling: " << (replace_existing ? "replace managed tracks" : "append/keep existing tracks") << "\n\n"
        << "Preview:\n";
    for (const auto& item : preview) {
        out << "- " << item << "\n";
    }
    if (selected_count > static_cast<int>(preview.size())) {
        out << "- ...\n";
    }
    if (setup_soundcheck && soundcheck_count == 0) {
        out << "\nNo selected sources support ALT soundcheck. The setup will proceed in recording-only mode.\n";
    }
    return out.str();
}
#endif

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
        << "WINGuard Bridge Summary\n\n"
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
    // Non-macOS fallback path: keep the flow staged even though REAPER only gives
    // us basic dialogs here.
    if (!extension.IsConnected()) {
        const bool connected = extension.ConnectToWing();
        if (!connected) {
            ShowMessageBox(
                "WINGuard could not connect.\n\n"
                "Set wing_ip in config.json and ensure OSC is enabled on the console.",
                "WINGuard",
                0);
            return;
        }
    }

    auto sources = extension.GetAvailableSources();
    if (sources.empty()) {
        ShowMessageBox(
            "Connected, but no selectable sources were discovered.",
            "WINGuard",
            0);
        return;
    }

    ShowMessageBox(BuildSourceCatalog(sources).c_str(),
                   "WINGuard: Source Selection Help",
                   0);

    char values[4096];
    const std::string default_selection = BuildDefaultSelectionSpec(sources);
    std::snprintf(values, sizeof(values), "%s,%s,%d",
                  default_selection.c_str(),
                  "SOUNDCHECK",
                  1);

    if (!GetUserInputs("WINGuard Setup Review",
                       3,
                       "Source selection (CURRENT/ALL_CH/ALL/ALL_SC or ranges),Mode (SOUNDCHECK/RECORD),Replace managed tracks (1/0)",
                       values,
                       sizeof(values))) {
        return;
    }

    const auto parts = SplitDelimited(values, ',');
    if (parts.size() != 3) {
        ShowMessageBox("Expected 3 values: selection, mode, replace flag.",
                       "WINGuard",
                       0);
        return;
    }

    std::vector<SourceSelectionInfo> selected_sources;
    std::string selection_error;
    if (!ParseSelectionSpec(parts[0], sources, selected_sources, selection_error)) {
        ShowMessageBox(selection_error.c_str(),
                       "WINGuard",
                       0);
        return;
    }

    std::string mode = parts[1];
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    bool setup_soundcheck = false;
    if (mode == "SOUNDCHECK" || mode == "SC") {
        setup_soundcheck = true;
    } else if (mode == "RECORD" || mode == "LIVE") {
        setup_soundcheck = false;
    } else {
        ShowMessageBox("Mode must be SOUNDCHECK or RECORD.",
                       "WINGuard",
                       0);
        return;
    }

    bool replace_existing = true;
    if (!ParseOnOffFlag(parts[2], replace_existing)) {
        ShowMessageBox("Replace managed tracks must be 1/0, ON/OFF, YES/NO, or TRUE/FALSE.",
                       "WINGuard",
                       0);
        return;
    }

    ShowMessageBox(BuildSelectionSummary(selected_sources, setup_soundcheck, replace_existing).c_str(),
                   "WINGuard: Review or Rebuild Setup",
                   0);

    char confirm_values[32];
    std::snprintf(confirm_values, sizeof(confirm_values), "%d", 0);
    if (!GetUserInputs("Apply or Rebuild WINGuard Setup",
                       1,
                       "Type 1 to apply this setup now, or 0 to cancel",
                       confirm_values,
                       sizeof(confirm_values))) {
        return;
    }
    bool apply_now = false;
    if (!ParseOnOffFlag(confirm_values, apply_now) || !apply_now) {
        ShowMessageBox("Setup cancelled before any routing changes were applied.",
                       "WINGuard",
                       0);
        return;
    }

    extension.SetupSoundcheckFromSelection(selected_sources, setup_soundcheck, replace_existing);
    ShowMessageBox("WINGuard setup applied. Reopen the action to review or stage another selection.",
                   "WINGuard",
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
    ShowMessageBox(output_help.str().c_str(), "WINGuard: Selected Channel Bridge", 0);

    char values[4096];
    std::snprintf(values, sizeof(values), "%d,%s,%d,%s,%d,%d",
                  config.bridge_midi_output_device,
                  config.bridge_midi_message_type.c_str(),
                  config.bridge_midi_channel,
                  BuildMappingSpec(config.bridge_mappings).c_str(),
                  config.bridge_enabled ? 1 : 0,
                  config.bridge_mappings.empty() ? 60 : config.bridge_mappings.front().midi_value);

    if (!GetUserInputs("WINGuard Bridge Setup",
                       6,
                       "MIDI output index,Behavior (NOTE_ON/NOTE_ON_OFF/PROGRAM),MIDI channel (1-16),Mappings,Enable bridge (0/1),Test MIDI value",
                       values,
                       sizeof(values))) {
        return false;
    }

    const auto parts = SplitDelimited(values, ',');
    if (parts.size() != 6) {
        ShowMessageBox("Expected 6 values from the bridge setup dialog.",
                       "WINGuard: Selected Channel Bridge",
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
                       "WINGuard: Selected Channel Bridge",
                       0);
        return false;
    }

    std::string behavior = parts[1];
    std::transform(behavior.begin(), behavior.end(), behavior.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (behavior != "NOTE_ON" && behavior != "NOTE_ON_OFF" && behavior != "PROGRAM") {
        ShowMessageBox("Behavior must be NOTE_ON, NOTE_ON_OFF, or PROGRAM.",
                       "WINGuard: Selected Channel Bridge",
                       0);
        return false;
    }
    if (output_index < -1 || output_index >= static_cast<int>(outputs.size())) {
        ShowMessageBox("MIDI output index is out of range for the detected outputs.",
                       "WINGuard: Selected Channel Bridge",
                       0);
        return false;
    }
    if (midi_channel < 1 || midi_channel > 16) {
        ShowMessageBox("MIDI channel must be between 1 and 16.",
                       "WINGuard: Selected Channel Bridge",
                       0);
        return false;
    }
    if (test_value < 0 || test_value > 127) {
        ShowMessageBox("Test MIDI value must be between 0 and 127.",
                       "WINGuard: Selected Channel Bridge",
                       0);
        return false;
    }

    std::vector<BridgeMapping> parsed_mappings;
    std::string mapping_error;
    if (!ParseMappingSpec(parts[3], parsed_mappings, mapping_error)) {
        ShowMessageBox(mapping_error.c_str(), "WINGuard: Selected Channel Bridge", 0);
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
                       "WINGuard: Selected Channel Bridge",
                       0);
        return false;
    }

    extension.ApplyBridgeSettings();

    std::string test_detail;
    extension.SendBridgeTestMessage(test_value, test_detail);

    std::ostringstream result;
    result << SelectedChannelBridgeSummary() << "\n" << test_detail;
    ShowMessageBox(result.str().c_str(), "WINGuard: Selected Channel Bridge", 0);
    return true;
}

} // namespace

void ShowMainDialog() {
#ifdef __APPLE__
    ShowWingConnectorDialogAtTab("console");
#else
    RunCrossPlatformDialog();
#endif
}

void ShowSelectedChannelBridgeDialog() {
    ShowBridgeSetupWizard();
}

void ShowExistingProjectAdoptionDialog() {
    auto& extension = ReaperExtension::Instance();
    auto& config = extension.GetConfig();

    if (!extension.IsConnected()) {
        if (config.wing_ip.empty()) {
            ShowMessageBox("Connect WINGuard to a WING first, or set a manual WING IP before running adoption review.",
                           "WINGuard: Existing Project Adoption",
                           0);
            return;
        }
        const bool connected = extension.ConnectToWing();
        if (!connected) {
            ShowMessageBox("WINGuard could not connect to the configured WING. Adoption review needs a live WING connection for channel metadata.",
                           "WINGuard: Existing Project Adoption",
                           0);
            return;
        }
    }

    const auto available_sources = extension.GetAvailableSources();
    const auto channel_sources = FilterChannelSources(available_sources);
    if (channel_sources.empty()) {
        ShowMessageBox("No WING channel sources were discovered. Adoption review cannot continue without live channel metadata.",
                       "WINGuard: Existing Project Adoption",
                       0);
        return;
    }

    const auto tracks = ScanProjectTracks();
    if (tracks.empty()) {
        ShowMessageBox("The current REAPER project has no tracks to review for adoption.",
                       "WINGuard: Existing Project Adoption",
                       0);
        return;
    }

    const auto suggestions = BuildAdoptionSuggestions(tracks, channel_sources);

    if (suggestions.empty()) {
        const std::string summary = BuildAdoptionReviewSummary(tracks, channel_sources, suggestions);
        ShowMessageBox(summary.c_str(), "WINGuard: Existing Project Adoption", 0);
        return;
    }

    std::map<int, SourceSelectionInfo> available_channel_map;
    for (const auto& source : channel_sources) {
        available_channel_map[source.source_number] = source;
    }
    const auto existing_routes = CollectExistingManagedRoutes();
    const std::string current_mode = (config.soundcheck_output_mode == "CARD") ? "CARD" : "USB";

#ifdef __APPLE__
    AdoptionDialogPlan preview_plan = BuildDefaultAdoptionPlan(suggestions, current_mode);
    std::string preview_error;
    std::vector<PlaybackAllocation> preview_allocations;
    BuildAdoptionRequestedAllocations(preview_plan, existing_routes, preview_allocations, preview_error);
    std::map<std::string, PlaybackAllocation> preview_by_source_id;
    for (const auto& allocation : preview_allocations) {
        SourceSelectionInfo source;
        source.kind = allocation.source_kind;
        source.source_number = allocation.source_number;
        preview_by_source_id[WingConnector::AdoptionPlan::SourcePersistentId(source)] = allocation;
    }
    std::vector<AdoptionEditorRow> editor_rows;
    editor_rows.reserve(suggestions.size());
    std::vector<int> available_channels;
    available_channels.reserve(channel_sources.size());
    for (const auto& source : channel_sources) {
        available_channels.push_back(source.source_number);
    }
    for (const auto& suggestion : suggestions) {
        AdoptionEditorRow row;
        row.track_index = suggestion.track.track_index;
        row.track_name = suggestion.track.name;
        row.stereo_like = suggestion.track.stereo_like;
        row.suggested_channel = suggestion.source.source_number;
        row.assigned_channel = suggestion.source.source_number;
        const std::string preview_source_id = WingConnector::AdoptionPlan::SourcePersistentId(suggestion.source);
        auto preview_it = preview_by_source_id.find(preview_source_id);
        if (preview_it != preview_by_source_id.end()) {
            row.suggested_slot_start = preview_it->second.usb_start;
            row.suggested_slot_end = preview_it->second.usb_end;
        }
        editor_rows.push_back(row);
    }
    std::string output_mode_value;
    std::string channel_overrides_value;
    std::string slot_overrides_value;
    bool apply_now = false;
    if (!ShowExistingProjectAdoptionEditor(editor_rows,
                                           available_channels,
                                           current_mode.c_str(),
                                           output_mode_value,
                                           channel_overrides_value,
                                           slot_overrides_value,
                                           apply_now)) {
        return;
    }

    AdoptionDialogPlan plan = BuildDefaultAdoptionPlan(suggestions, output_mode_value);
    std::string parse_error;
    if (!ParseAdoptionChannelAssignments(channel_overrides_value, available_channel_map, plan, parse_error)) {
        ShowMessageBox(parse_error.c_str(), "WINGuard: Existing Project Adoption", 0);
        return;
    }
    if (!ParseAdoptionSlotOverrides(slot_overrides_value, plan, parse_error)) {
        ShowMessageBox(parse_error.c_str(), "WINGuard: Existing Project Adoption", 0);
        return;
    }
    if (!ValidateAdoptionAssignments(plan, existing_routes, parse_error)) {
        ShowMessageBox(parse_error.c_str(), "WINGuard: Existing Project Adoption", 0);
        return;
    }
    if (!apply_now) {
        ShowMessageBox("In-place adoption cancelled before any routing or metadata changes were applied.",
                       "WINGuard: Existing Project Adoption",
                       0);
        return;
    }
#else
    char values[4096];
    std::snprintf(values,
                  sizeof(values),
                  "%s,%s,%s,%d",
                  current_mode.c_str(),
                  "",
                  "",
                  0);

    if (!GetUserInputs("Editable Existing Project Adoption",
                       4,
                       "Mode (USB/CARD),Channel overrides (2=CH8;3=CH2),Slot overrides (2=9-10;3=4),Apply now (1/0)",
                       values,
                       sizeof(values))) {
        return;
    }

    const auto parts = SplitDelimited(values, ',');
    if (parts.size() != 4) {
        ShowMessageBox("Expected 4 values: mode, channel overrides, slot overrides, and apply flag.",
                       "WINGuard: Existing Project Adoption",
                       0);
        return;
    }

    AdoptionDialogPlan plan = BuildDefaultAdoptionPlan(suggestions, parts[0]);
    std::string parse_error;
    if (!ParseAdoptionChannelAssignments(parts[1], available_channel_map, plan, parse_error)) {
        ShowMessageBox(parse_error.c_str(), "WINGuard: Existing Project Adoption", 0);
        return;
    }
    if (!ParseAdoptionSlotOverrides(parts[2], plan, parse_error)) {
        ShowMessageBox(parse_error.c_str(), "WINGuard: Existing Project Adoption", 0);
        return;
    }
    if (!ValidateAdoptionAssignments(plan, existing_routes, parse_error)) {
        ShowMessageBox(parse_error.c_str(), "WINGuard: Existing Project Adoption", 0);
        return;
    }

    bool apply_now = false;
    if (!ParseSimpleOnOffFlag(parts[3], apply_now) || !apply_now) {
        ShowMessageBox("In-place adoption cancelled before any routing or metadata changes were applied.",
                       "WINGuard: Existing Project Adoption",
                       0);
        return;
    }
#endif

    std::vector<PlaybackAllocation> requested_allocations;
    if (!BuildAdoptionRequestedAllocations(plan, existing_routes, requested_allocations, parse_error)) {
        ShowMessageBox(parse_error.c_str(), "WINGuard: Existing Project Adoption", 0);
        return;
    }

    auto selected_sources = BuildSelectedSourcesFromPlan(plan);
    const auto metadata_snapshots = CaptureAdoptionTrackMetadata(plan);
    MarkTracksAdoptedInPlace(plan);
    const bool applied = extension.SetupSoundcheckFromPlan(selected_sources,
                                                           requested_allocations,
                                                           plan.output_mode,
                                                           true,
                                                           false);
    if (!applied) {
        RestoreAdoptionTrackMetadata(metadata_snapshots);
        ShowMessageBox("Adoption apply did not complete. Existing imported tracks were left unmanaged.",
                       "WINGuard: Existing Project Adoption",
                       0);
        return;
    }

    std::string validation_details;
    const ValidationState validation_state = extension.ValidateLiveRecordingSetup(validation_details);
    std::ostringstream result;
    result << "In-place adoption completed for " << selected_sources.size() << " channel mappings.\n\n"
           << "Mode: " << plan.output_mode << "\n"
           << "Behavior: imported tracks were marked as managed and their routing was rewritten in place.\n\n";

    if (validation_state == ValidationState::Ready) {
        result << "Validation: ready.\n";
    } else if (validation_state == ValidationState::Warning) {
        result << "Validation: warning.\n";
    } else {
        result << "Validation: not ready.\n";
    }
    if (!validation_details.empty()) {
        result << validation_details << "\n\n";
    }
    result << "Use the main WINGuard action to switch Live/Soundcheck mode once the setup is ready.";
    ShowMessageBox(result.str().c_str(), "WINGuard: Existing Project Adoption", 0);
}

} // namespace WingConnector
