#include "internal/playback_naming.h"

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

void TestStereoInputUsesBaseName() {
    Expect(WingConnector::PlaybackNaming::StereoInputName("OH") == "OH",
           "stereo inputs should keep the base track name");
}

void TestStereoOutputsUseSideSuffixes() {
    Expect(WingConnector::PlaybackNaming::StereoOutputLeftName("OH") == "OH (L)",
           "left stereo output should use the left suffix");
    Expect(WingConnector::PlaybackNaming::StereoOutputRightName("OH") == "OH (R)",
           "right stereo output should use the right suffix");
}

void TestEmptyNamesStayPredictable() {
    Expect(WingConnector::PlaybackNaming::StereoInputName("") == "",
           "empty stereo input names should stay empty");
    Expect(WingConnector::PlaybackNaming::StereoOutputLeftName("") == " (L)",
           "empty left output names should still use the suffix form");
    Expect(WingConnector::PlaybackNaming::StereoOutputRightName("") == " (R)",
           "empty right output names should still use the suffix form");
}

void TestExistingSuffixesAreNotNormalizedAway() {
    Expect(WingConnector::PlaybackNaming::StereoOutputLeftName("OH (L)") == "OH (L) (L)",
           "left naming should remain a simple suffix append");
    Expect(WingConnector::PlaybackNaming::StereoOutputRightName("OH (R)") == "OH (R) (R)",
           "right naming should remain a simple suffix append");
}

}  // namespace

int main() {
    TestStereoInputUsesBaseName();
    TestStereoOutputsUseSideSuffixes();
    TestEmptyNamesStayPredictable();
    TestExistingSuffixesAreNotNormalizedAway();
    std::cout << "playback_naming_tests: OK\n";
    return 0;
}
