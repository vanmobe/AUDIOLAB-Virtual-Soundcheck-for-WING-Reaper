#include "internal/soundcheck_state.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void TestBuildPaths() {
    const std::vector<int> channels{1, 12};
    const auto enabled_paths = WingConnector::SoundcheckState::BuildAltEnabledPaths(channels);
    const auto group_paths = WingConnector::SoundcheckState::BuildAltGroupPaths(channels);
    Expect(enabled_paths.size() == 2, "enabled path count should match channels");
    Expect(group_paths.size() == 2, "group path count should match channels");
    Expect(enabled_paths[0] == "/ch/1/in/set/altsrc", "channel 1 enabled path should match");
    Expect(group_paths[1] == "/ch/12/in/conn/altgrp", "channel 12 group path should match");
}

void TestUsbModeDetectsManagedSoundcheck() {
    const std::vector<std::string> enabled_paths{"/ch/1/in/set/altsrc"};
    const std::vector<std::string> group_paths{"/ch/1/in/conn/altgrp"};
    const std::map<std::string, int> alt_enabled{{enabled_paths[0], 1}};
    const std::map<std::string, std::string> alt_groups{{group_paths[0], "USB"}};
    Expect(WingConnector::SoundcheckState::IsManagedSoundcheckActive(
               enabled_paths, group_paths, alt_enabled, alt_groups, "USB"),
           "USB mode should detect active USB ALT source");
}

void TestCardModeAcceptsCrdAlias() {
    const std::vector<std::string> enabled_paths{"/ch/2/in/set/altsrc"};
    const std::vector<std::string> group_paths{"/ch/2/in/conn/altgrp"};
    const std::map<std::string, int> alt_enabled{{enabled_paths[0], 1}};
    const std::map<std::string, std::string> alt_groups{{group_paths[0], "CRD"}};
    Expect(WingConnector::SoundcheckState::IsManagedSoundcheckActive(
               enabled_paths, group_paths, alt_enabled, alt_groups, "CARD"),
           "CARD mode should accept CRD alias reported by WING");
}

void TestDisabledAltDoesNotTrigger() {
    const std::vector<std::string> enabled_paths{"/ch/3/in/set/altsrc"};
    const std::vector<std::string> group_paths{"/ch/3/in/conn/altgrp"};
    const std::map<std::string, int> alt_enabled{{enabled_paths[0], 0}};
    const std::map<std::string, std::string> alt_groups{{group_paths[0], "USB"}};
    Expect(!WingConnector::SoundcheckState::IsManagedSoundcheckActive(
               enabled_paths, group_paths, alt_enabled, alt_groups, "USB"),
           "disabled ALT source should not trigger soundcheck mode");
}

void TestMismatchedGroupDoesNotTrigger() {
    const std::vector<std::string> enabled_paths{"/ch/4/in/set/altsrc"};
    const std::vector<std::string> group_paths{"/ch/4/in/conn/altgrp"};
    const std::map<std::string, int> alt_enabled{{enabled_paths[0], 1}};
    const std::map<std::string, std::string> alt_groups{{group_paths[0], "USB"}};
    Expect(!WingConnector::SoundcheckState::IsManagedSoundcheckActive(
               enabled_paths, group_paths, alt_enabled, alt_groups, "CARD"),
           "USB ALT group should not trigger CARD soundcheck mode");
}

}  // namespace

int main() {
    TestBuildPaths();
    TestUsbModeDetectsManagedSoundcheck();
    TestCardModeAcceptsCrdAlias();
    TestDisabledAltDoesNotTrigger();
    TestMismatchedGroupDoesNotTrigger();

    std::cout << "soundcheck_state_tests: OK\n";
    return 0;
}
