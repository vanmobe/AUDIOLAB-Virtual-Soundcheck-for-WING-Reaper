#include "internal/adoption_plan.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using WingConnector::SourceKind;
using WingConnector::SourceSelectionInfo;
using WingConnector::PlaybackAllocation;
namespace AdoptionPlan = WingConnector::AdoptionPlan;

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

SourceSelectionInfo MakeChannel(int channel_number, const std::string& wing_name = std::string()) {
    SourceSelectionInfo source;
    source.kind = SourceKind::Channel;
    source.source_number = channel_number;
    source.name = wing_name;
    source.selected = true;
    source.soundcheck_capable = true;
    return source;
}

AdoptionPlan::Row MakeRow(int track_index,
                          const std::string& track_name,
                          bool stereo_like,
                          int channel_number,
                          const std::string& wing_name = std::string()) {
    AdoptionPlan::Row row;
    row.track.track_index = track_index;
    row.track.name = track_name;
    row.track.stereo_like = stereo_like;
    row.assigned_source = MakeChannel(channel_number, wing_name);
    return row;
}

void TestBuildSelectedSourcesPreservesImportedTrackName() {
    const std::vector<AdoptionPlan::Row> rows{
        MakeRow(8, "OH", true, 8, "CH8"),
    };

    const auto selected = AdoptionPlan::BuildSelectedSources(rows);
    Expect(selected.size() == 1, "one selected source should be built");
    Expect(selected[0].name == "OH", "imported REAPER track name should win over WING channel name");
    Expect(selected[0].stereo_linked, "stereo intent should be preserved");
    Expect(selected[0].stereo_intent_override, "stereo override should be enabled for adopted stereo tracks");
}

void TestValidateAssignmentsRejectsDuplicateOverrideAgainstExistingRoute() {
    const std::vector<AdoptionPlan::Row> rows = [] {
        auto row = MakeRow(2, "Kick", false, 2);
        row.slot_overridden = true;
        row.slot_start = 3;
        row.slot_end = 3;
        return std::vector<AdoptionPlan::Row>{row};
    }();

    const std::vector<AdoptionPlan::ExistingRoute> existing{
        {"CH:1", 3, 3},
    };

    std::string error;
    Expect(!AdoptionPlan::ValidateAssignments(rows, existing, "USB", error),
           "duplicate manual override should be rejected");
    Expect(error.find("Playback slot 3") != std::string::npos,
           "duplicate-slot error should mention the occupied slot");
}

void TestValidateAssignmentsRejectsDuplicateChannelAssignments() {
    const std::vector<AdoptionPlan::Row> rows{
        MakeRow(2, "Kick", false, 8),
        MakeRow(3, "Snare", false, 8),
    };

    std::string error;
    Expect(!AdoptionPlan::ValidateAssignments(rows, {}, "USB", error),
           "duplicate WING channel assignments should be rejected");
    Expect(error.find("only be assigned once") != std::string::npos,
           "duplicate-channel error should explain the conflict");
}

void TestValidateAssignmentsRejectsEvenStereoSlotOverride() {
    auto row = MakeRow(8, "OH", true, 8);
    row.slot_overridden = true;
    row.slot_start = 10;
    row.slot_end = 11;

    std::string error;
    Expect(!AdoptionPlan::ValidateAssignments({row}, {}, "USB", error),
           "stereo override starting on an even slot should be rejected");
    Expect(error.find("odd playback slot") != std::string::npos,
           "stereo-slot error should mention the odd-start rule");
}

void TestValidateAssignmentsRejectsStereoAssignmentWithoutPartnerChannel() {
    const std::vector<AdoptionPlan::Row> rows{
        MakeRow(40, "Audience", true, 40),
    };

    std::string error;
    Expect(!AdoptionPlan::ValidateAssignments(rows, {}, "USB", error),
           "stereo assignment should fail when the target channel has no partner");
    Expect(error.find("partner channel") != std::string::npos,
           "missing-partner error should explain the stereo channel constraint");
}

void TestBuildRequestedAllocationsSkipsOccupiedStereoPairs() {
    const std::vector<AdoptionPlan::Row> rows{
        MakeRow(8, "OH", true, 8),
        MakeRow(9, "Room", false, 9),
    };

    const std::vector<AdoptionPlan::ExistingRoute> existing{
        {"CH:1", 1, 1},
        {"CH:2", 2, 2},
        {"CH:3", 3, 3},
        {"CH:4", 4, 4},
        {"CH:5", 5, 5},
        {"CH:6", 6, 6},
        {"CH:7", 7, 7},
        {"CH:10", 8, 8},
    };

    std::vector<PlaybackAllocation> allocations;
    std::string error;
    Expect(AdoptionPlan::BuildRequestedAllocations(rows, existing, "USB", allocations, error),
           "allocation build should succeed with later free slots");
    Expect(allocations.size() == 2, "two allocations should be returned");
    Expect(allocations[0].usb_start == 9 && allocations[0].usb_end == 10,
           "first stereo row should move to the next free odd stereo pair");
    Expect(allocations[1].usb_start == 11 && allocations[1].usb_end == 11,
           "later mono row should fill the next free single slot");
}

void TestBuildRequestedAllocationsHonorsManualOverride() {
    auto row = MakeRow(4, "Snare", false, 4);
    row.slot_overridden = true;
    row.slot_start = 12;
    row.slot_end = 12;

    std::vector<PlaybackAllocation> allocations;
    std::string error;
    Expect(AdoptionPlan::BuildRequestedAllocations({row}, {}, "CARD", allocations, error),
           "manual override allocation should succeed");
    Expect(allocations.size() == 1, "one allocation should be returned");
    Expect(allocations[0].usb_start == 12 && allocations[0].usb_end == 12,
           "manual override slot should be preserved");
    Expect(allocations[0].allocation_note == "Manual override",
           "manual override allocations should be labeled");
}

void TestBuildRequestedAllocationsUsesCardBankLimit() {
    std::vector<AdoptionPlan::ExistingRoute> existing;
    for (int slot = 1; slot <= 32; ++slot) {
        existing.push_back({"CH:" + std::to_string(slot), slot, slot});
    }

    const std::vector<AdoptionPlan::Row> rows{
        MakeRow(33, "Spare", false, 33),
    };

    std::vector<PlaybackAllocation> allocations;
    std::string error;
    Expect(!AdoptionPlan::BuildRequestedAllocations(rows, existing, "CARD", allocations, error),
           "CARD allocation should fail when all 32 slots are already occupied");
    Expect(error.find("No free playback slot") != std::string::npos,
           "CARD exhaustion should report that no playback slot is available");
}

void TestBuildRequestedAllocationsAcceptsCardStereoPair() {
    const std::vector<AdoptionPlan::ExistingRoute> existing{
        {"CH:1", 1, 1},
        {"CH:2", 2, 2},
        {"CH:3", 3, 3},
        {"CH:4", 4, 4},
        {"CH:5", 5, 5},
        {"CH:6", 6, 6},
    };

    const std::vector<AdoptionPlan::Row> rows{
        MakeRow(8, "OH", true, 8),
    };

    std::vector<PlaybackAllocation> allocations;
    std::string error;
    Expect(AdoptionPlan::BuildRequestedAllocations(rows, existing, "CARD", allocations, error),
           "CARD allocation should support stereo rows");
    Expect(allocations.size() == 1, "one CARD stereo allocation should be returned");
    Expect(allocations[0].usb_start == 7 && allocations[0].usb_end == 8,
           "CARD stereo allocation should still use the next odd-start pair");
}

void TestBuildRequestedAllocationsFollowAssignedChannelOrder() {
    const std::vector<AdoptionPlan::Row> rows{
        MakeRow(8, "OH", true, 8),
        MakeRow(2, "Kick", false, 1),
        MakeRow(3, "Snare", false, 2),
    };

    std::vector<PlaybackAllocation> allocations;
    std::string error;
    Expect(AdoptionPlan::BuildRequestedAllocations(rows, {}, "USB", allocations, error),
           "allocation build should succeed for out-of-order rows");
    Expect(allocations.size() == 3, "three allocations should be returned");
    Expect(allocations[0].source_number == 1 && allocations[0].usb_start == 1 && allocations[0].usb_end == 1,
           "lowest assigned channel should claim the first slot");
    Expect(allocations[1].source_number == 2 && allocations[1].usb_start == 2 && allocations[1].usb_end == 2,
           "next assigned channel should claim the next slot");
    Expect(allocations[2].source_number == 8 && allocations[2].usb_start == 3 && allocations[2].usb_end == 4,
           "stereo channel should be routed after lower assigned channels, not by track row order");
}

}  // namespace

int main() {
    TestBuildSelectedSourcesPreservesImportedTrackName();
    TestValidateAssignmentsRejectsDuplicateOverrideAgainstExistingRoute();
    TestValidateAssignmentsRejectsDuplicateChannelAssignments();
    TestValidateAssignmentsRejectsEvenStereoSlotOverride();
    TestValidateAssignmentsRejectsStereoAssignmentWithoutPartnerChannel();
    TestBuildRequestedAllocationsSkipsOccupiedStereoPairs();
    TestBuildRequestedAllocationsHonorsManualOverride();
    TestBuildRequestedAllocationsUsesCardBankLimit();
    TestBuildRequestedAllocationsAcceptsCardStereoPair();
    TestBuildRequestedAllocationsFollowAssignedChannelOrder();

    std::cout << "adoption_plan_tests: OK\n";
    return 0;
}
