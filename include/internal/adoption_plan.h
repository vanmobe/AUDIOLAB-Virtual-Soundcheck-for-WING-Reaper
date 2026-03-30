#ifndef WINGCONNECTOR_INTERNAL_ADOPTION_PLAN_H
#define WINGCONNECTOR_INTERNAL_ADOPTION_PLAN_H

#include "wingconnector/wing_osc.h"
#include "internal/stereo_channel_plan.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace WingConnector::AdoptionPlan {

struct TrackRef {
    int track_index = 0;
    std::string name;
    bool stereo_like = false;
};

struct Row {
    TrackRef track;
    SourceSelectionInfo assigned_source;
    bool slot_overridden = false;
    int slot_start = 0;
    int slot_end = 0;
};

struct ExistingRoute {
    std::string source_id;
    int slot_start = 0;
    int slot_end = 0;
};

inline std::string SourcePersistentId(const SourceSelectionInfo& source) {
    switch (source.kind) {
        case SourceKind::Channel:
            return "CH:" + std::to_string(source.source_number);
        case SourceKind::Bus:
            return "BUS:" + std::to_string(source.source_number);
        case SourceKind::Main:
            return "MAIN:" + std::to_string(source.source_number);
        case SourceKind::Matrix:
            return "MTX:" + std::to_string(source.source_number);
    }
    return "CH:" + std::to_string(source.source_number);
}

inline std::string NormalizeOutputMode(const std::string& value) {
    return value == "CARD" ? "CARD" : "USB";
}

inline std::vector<SourceSelectionInfo> BuildSelectedSources(const std::vector<Row>& rows) {
    std::vector<SourceSelectionInfo> selected_sources;
    selected_sources.reserve(rows.size());
    for (const auto& row : rows) {
        SourceSelectionInfo selected = row.assigned_source;
        if (!row.track.name.empty()) {
            selected.name = row.track.name;
        }
        selected.selected = true;
        selected.stereo_linked = row.track.stereo_like;
        selected.stereo_intent_override = row.track.stereo_like;
        selected_sources.push_back(selected);
    }
    return selected_sources;
}

inline bool ValidateAssignments(const std::vector<Row>& rows,
                                const std::vector<ExistingRoute>& existing_routes,
                                const std::string& output_mode,
                                std::string& error_out) {
    std::set<int> seen_channels;
    for (const auto& row : rows) {
        if (!seen_channels.insert(row.assigned_source.source_number).second) {
            error_out = "Each WING channel can only be assigned once in the adoption plan.";
            return false;
        }
        if (row.track.stereo_like &&
            row.assigned_source.kind == SourceKind::Channel &&
            !WingConnector::StereoChannelPlan::HasStereoPartner(row.assigned_source.source_number)) {
            error_out = "Stereo channel adoption requires a following partner channel on the WING.";
            return false;
        }
    }

    const int slot_limit = NormalizeOutputMode(output_mode) == "CARD" ? 32 : 48;
    std::set<int> occupied_slots;
    std::set<std::string> adopted_source_ids;
    for (const auto& row : rows) {
        adopted_source_ids.insert(SourcePersistentId(row.assigned_source));
    }
    for (const auto& route : existing_routes) {
        if (adopted_source_ids.count(route.source_id) > 0) {
            continue;
        }
        for (int slot = route.slot_start; slot <= route.slot_end; ++slot) {
            occupied_slots.insert(slot);
        }
    }

    for (const auto& row : rows) {
        if (!row.slot_overridden) {
            continue;
        }
        if (row.slot_start < 1 || row.slot_end > slot_limit) {
            error_out = NormalizeOutputMode(output_mode) + " slot override exceeds the available Wing bank.";
            return false;
        }
        if (row.track.stereo_like) {
            if (row.slot_end != row.slot_start + 1) {
                error_out = "Stereo rows must use a contiguous slot pair.";
                return false;
            }
            if ((row.slot_start % 2) == 0) {
                error_out = "Stereo rows must start on an odd playback slot.";
                return false;
            }
        } else if (row.slot_end != row.slot_start) {
            error_out = "Mono rows can only use a single playback slot.";
            return false;
        }

        for (int slot = row.slot_start; slot <= row.slot_end; ++slot) {
            if (!occupied_slots.insert(slot).second) {
                error_out = "Playback slot " + std::to_string(slot) +
                            " is already reserved by another managed or adopted track.";
                return false;
            }
        }
    }
    return true;
}

inline bool BuildRequestedAllocations(const std::vector<Row>& rows,
                                      const std::vector<ExistingRoute>& existing_routes,
                                      const std::string& output_mode,
                                      std::vector<PlaybackAllocation>& allocations_out,
                                      std::string& error_out) {
    allocations_out.clear();
    const int slot_limit = NormalizeOutputMode(output_mode) == "CARD" ? 32 : 48;

    std::set<int> occupied_slots;
    std::set<std::string> adopted_source_ids;
    for (const auto& row : rows) {
        adopted_source_ids.insert(SourcePersistentId(row.assigned_source));
    }
    for (const auto& route : existing_routes) {
        if (adopted_source_ids.count(route.source_id) > 0) {
            continue;
        }
        for (int slot = route.slot_start; slot <= route.slot_end; ++slot) {
            occupied_slots.insert(slot);
        }
    }

    std::vector<Row> ordered_rows = rows;
    std::stable_sort(ordered_rows.begin(), ordered_rows.end(), [](const Row& lhs, const Row& rhs) {
        if (lhs.assigned_source.kind != rhs.assigned_source.kind) {
            return static_cast<int>(lhs.assigned_source.kind) < static_cast<int>(rhs.assigned_source.kind);
        }
        if (lhs.assigned_source.source_number != rhs.assigned_source.source_number) {
            return lhs.assigned_source.source_number < rhs.assigned_source.source_number;
        }
        return lhs.track.track_index < rhs.track.track_index;
    });

    allocations_out.reserve(ordered_rows.size());
    for (const auto& row : ordered_rows) {
        PlaybackAllocation allocation;
        allocation.source_kind = row.assigned_source.kind;
        allocation.source_number = row.assigned_source.source_number;
        allocation.is_stereo = row.track.stereo_like;

        if (row.slot_overridden) {
            allocation.usb_start = row.slot_start;
            allocation.usb_end = row.slot_end;
            allocation.allocation_note = "Manual override";
            for (int slot = allocation.usb_start; slot <= allocation.usb_end; ++slot) {
                occupied_slots.insert(slot);
            }
            allocations_out.push_back(allocation);
            continue;
        }

        if (allocation.is_stereo) {
            bool found = false;
            for (int slot = 1; slot + 1 <= slot_limit; slot += 2) {
                if (occupied_slots.count(slot) == 0 && occupied_slots.count(slot + 1) == 0) {
                    allocation.usb_start = slot;
                    allocation.usb_end = slot + 1;
                    allocation.allocation_note = "Auto pair";
                    occupied_slots.insert(slot);
                    occupied_slots.insert(slot + 1);
                    found = true;
                    break;
                }
            }
            if (!found) {
                error_out = "No free odd-start stereo pair is available for track " +
                            std::to_string(row.track.track_index) + ".";
                return false;
            }
        } else {
            bool found = false;
            for (int slot = 1; slot <= slot_limit; ++slot) {
                if (occupied_slots.count(slot) == 0) {
                    allocation.usb_start = slot;
                    allocation.usb_end = slot;
                    allocation.allocation_note = "Auto fill";
                    occupied_slots.insert(slot);
                    found = true;
                    break;
                }
            }
            if (!found) {
                error_out = "No free playback slot is available for track " +
                            std::to_string(row.track.track_index) + ".";
                return false;
            }
        }

        allocations_out.push_back(allocation);
    }
    return true;
}

}  // namespace WingConnector::AdoptionPlan

#endif
