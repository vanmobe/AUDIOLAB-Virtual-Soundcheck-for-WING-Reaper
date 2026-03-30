#include "internal/source_input_mode_plan.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void TestOddInputCanApplyStereoInPlace() {
    Expect(WingConnector::SourceInputModePlan::CanApplyStereoInPlace(7),
           "odd-start source inputs should allow in-place stereo mode writes");
}

void TestEvenInputCannotApplyStereoInPlace() {
    Expect(!WingConnector::SourceInputModePlan::CanApplyStereoInPlace(8),
           "even-start source inputs should not allow in-place stereo mode writes");
}

void TestStereoPrimaryInputFollowsPlaybackPair() {
    Expect(WingConnector::SourceInputModePlan::PlannedStereoPrimaryInputStart(8, 9) == 9,
           "stereo primary routing should follow the adopted odd playback pair");
    Expect(WingConnector::SourceInputModePlan::PlannedStereoPrimaryInputStart(7, 9) == 9,
           "stereo primary routing should move to the adopted pair even when the old source was already odd");
}

void TestStereoPrimaryInputFallsBackToExistingOddPair() {
    Expect(WingConnector::SourceInputModePlan::PlannedStereoPrimaryInputStart(7, 0) == 7,
           "without an adopted playback pair, an existing odd-start source pair should still be usable");
    Expect(WingConnector::SourceInputModePlan::PlannedStereoPrimaryInputStart(8, 0) == 0,
           "without an adopted playback pair, an even-start source cannot become stereo safely");
}

}  // namespace

int main() {
    TestOddInputCanApplyStereoInPlace();
    TestEvenInputCannotApplyStereoInPlace();
    TestStereoPrimaryInputFollowsPlaybackPair();
    TestStereoPrimaryInputFallsBackToExistingOddPair();

    std::cout << "source_input_mode_plan_tests: OK\n";
    return 0;
}
