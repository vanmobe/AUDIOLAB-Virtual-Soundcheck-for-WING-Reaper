#include "internal/stereo_channel_plan.h"

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

void TestPartnerChannelFollowsAssignedChannel() {
    Expect(WingConnector::StereoChannelPlan::PartnerChannel(8) == 9,
           "stereo adoption on CH8 should manage CH9 as the partner channel");
    Expect(WingConnector::StereoChannelPlan::PartnerChannel(39) == 40,
           "the last valid stereo pair should be CH39-CH40");
}

void TestInvalidPartnerChannelIsRejected() {
    Expect(!WingConnector::StereoChannelPlan::HasStereoPartner(40),
           "CH40 cannot start a new stereo pair");
    Expect(WingConnector::StereoChannelPlan::PartnerChannel(40) == 0,
           "invalid stereo starts should not produce a partner channel");
}

}  // namespace

int main() {
    TestPartnerChannelFollowsAssignedChannel();
    TestInvalidPartnerChannelIsRejected();

    std::cout << "stereo_channel_plan_tests: OK\n";
    return 0;
}
