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

}  // namespace

int main() {
    TestStereoInputUsesBaseName();
    TestStereoOutputsUseSideSuffixes();
    std::cout << "playback_naming_tests: OK\n";
    return 0;
}
