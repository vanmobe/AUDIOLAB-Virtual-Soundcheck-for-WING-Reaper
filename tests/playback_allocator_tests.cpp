#include "internal/playback_allocator.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using WingConnector::PlaybackAllocation;
using WingConnector::SourceKind;
using WingConnector::SourceSelectionInfo;
namespace PlaybackAllocator = WingConnector::PlaybackAllocator;

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

SourceSelectionInfo MakeSource(int number, bool stereo) {
    SourceSelectionInfo source;
    source.kind = SourceKind::Channel;
    source.source_number = number;
    source.stereo_linked = stereo;
    source.selected = true;
    return source;
}

void TestStereoForcesOddStartAndCreatesGap() {
    const auto allocations = PlaybackAllocator::BuildSequentialPlaybackAllocation({
        MakeSource(1, false),
        MakeSource(2, true),
    });
    Expect(allocations.size() == 2, "two allocations should be returned");
    Expect(allocations[0].usb_start == 1 && allocations[0].usb_end == 1,
           "first mono source should take slot 1");
    Expect(allocations[1].usb_start == 3 && allocations[1].usb_end == 4,
           "stereo source should advance to the next odd-start pair");
}

void TestMonoBackfillsStereoGap() {
    const auto allocations = PlaybackAllocator::BuildSequentialPlaybackAllocation({
        MakeSource(1, false),
        MakeSource(2, true),
        MakeSource(3, false),
    });
    Expect(allocations.size() == 3, "three allocations should be returned");
    Expect(allocations[2].usb_start == 2 && allocations[2].usb_end == 2,
           "later mono source should backfill the held gap slot");
}

void TestStereoSequencePreservesOddPairs() {
    const auto allocations = PlaybackAllocator::BuildSequentialPlaybackAllocation({
        MakeSource(1, true),
        MakeSource(2, true),
    });
    Expect(allocations.size() == 2, "two stereo allocations should be returned");
    Expect(allocations[0].usb_start == 1 && allocations[0].usb_end == 2,
           "first stereo source should use 1-2");
    Expect(allocations[1].usb_start == 3 && allocations[1].usb_end == 4,
           "second stereo source should use 3-4");
}

}  // namespace

int main() {
    TestStereoForcesOddStartAndCreatesGap();
    TestMonoBackfillsStereoGap();
    TestStereoSequencePreservesOddPairs();

    std::cout << "playback_allocator_tests: OK\n";
    return 0;
}
