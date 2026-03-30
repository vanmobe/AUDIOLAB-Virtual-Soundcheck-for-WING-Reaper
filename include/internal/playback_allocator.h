#ifndef WINGCONNECTOR_INTERNAL_PLAYBACK_ALLOCATOR_H
#define WINGCONNECTOR_INTERNAL_PLAYBACK_ALLOCATOR_H

#include "wingconnector/wing_osc.h"

#include <string>
#include <vector>

namespace WingConnector::PlaybackAllocator {

inline std::vector<PlaybackAllocation> BuildSequentialPlaybackAllocation(const std::vector<SourceSelectionInfo>& channels) {
    std::vector<PlaybackAllocation> allocations;

    int next_slot = 1;
    std::vector<int> gap_slots;

    for (const auto& source : channels) {
        PlaybackAllocation allocation;
        allocation.source_kind = source.kind;
        allocation.source_number = source.source_number;
        allocation.is_stereo = source.stereo_linked;

        if (source.stereo_linked) {
            if ((next_slot % 2) == 0) {
                gap_slots.push_back(next_slot);
                next_slot++;
            }
            allocation.usb_start = next_slot;
            allocation.usb_end = next_slot + 1;
            allocation.allocation_note = "Stereo on slot " + std::to_string(next_slot) + "-" +
                                         std::to_string(next_slot + 1);
            next_slot += 2;
        } else {
            int slot = 0;
            if (!gap_slots.empty()) {
                slot = gap_slots.front();
                gap_slots.erase(gap_slots.begin());
            } else {
                slot = next_slot++;
            }
            allocation.usb_start = slot;
            allocation.usb_end = slot;
            allocation.allocation_note = "Mono on slot " + std::to_string(slot);
        }

        allocations.push_back(allocation);
    }

    return allocations;
}

}  // namespace WingConnector::PlaybackAllocator

#endif
