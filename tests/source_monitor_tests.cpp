#include "internal/managed_source_monitor.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using WingConnector::ManagedChannelInputState;
using WingConnector::ManagedSourceMonitor::Action;
using WingConnector::ManagedSourceMonitor::ClassifyChange;

namespace {

ManagedChannelInputState MakeState(int channel_number,
                                   const std::string& group,
                                   int input,
                                   bool stereo,
                                   bool readable = true) {
    ManagedChannelInputState state;
    state.channel_number = channel_number;
    state.source_group = group;
    state.source_input = input;
    state.stereo_linked = stereo;
    state.readable = readable;
    return state;
}

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void ExpectChannels(const std::vector<int>& actual,
                    const std::vector<int>& expected,
                    const std::string& message) {
    Expect(actual == expected, message);
}

void TestNoChange() {
    std::map<int, ManagedChannelInputState> previous{{1, MakeState(1, "A", 1, false)}};
    auto decision = ClassifyChange(previous, previous);
    Expect(decision.action == Action::None, "unchanged state should not trigger a decision");
    Expect(decision.changed_channels.empty(), "unchanged state should have no changed channels");
}

void TestMonoSourceChangeReapplies() {
    std::map<int, ManagedChannelInputState> previous{{1, MakeState(1, "A", 1, false)}};
    std::map<int, ManagedChannelInputState> current{{1, MakeState(1, "B", 5, false)}};
    auto decision = ClassifyChange(previous, current);
    Expect(decision.action == Action::ReapplyRouting, "mono source change should trigger routing reapply");
    ExpectChannels(decision.changed_channels, {1}, "mono source change should identify the changed channel");
}

void TestStereoSourceChangeReapplies() {
    std::map<int, ManagedChannelInputState> previous{{2, MakeState(2, "USB", 9, true)}};
    std::map<int, ManagedChannelInputState> current{{2, MakeState(2, "USB", 17, true)}};
    auto decision = ClassifyChange(previous, current);
    Expect(decision.action == Action::ReapplyRouting, "stereo source change should trigger routing reapply");
    ExpectChannels(decision.changed_channels, {2}, "stereo source change should identify the changed channel");
}

void TestTopologyChangeWarns() {
    std::map<int, ManagedChannelInputState> previous{{3, MakeState(3, "A", 3, false)}};
    std::map<int, ManagedChannelInputState> current{{3, MakeState(3, "A", 3, true)}};
    auto decision = ClassifyChange(previous, current);
    Expect(decision.action == Action::WarnTopologyChange, "mono/stereo topology change should warn");
    ExpectChannels(decision.changed_channels, {3}, "topology change should identify the changed channel");
}

void TestInvalidSourceWarns() {
    std::map<int, ManagedChannelInputState> previous{{4, MakeState(4, "A", 4, false)}};
    std::map<int, ManagedChannelInputState> current{{4, MakeState(4, "OFF", 0, false)}};
    auto decision = ClassifyChange(previous, current);
    Expect(decision.action == Action::WarnInvalidSource, "OFF source should warn instead of rerouting");
    ExpectChannels(decision.changed_channels, {4}, "invalid source should identify the changed channel");
}

void TestUnreadableSourceWarns() {
    std::map<int, ManagedChannelInputState> previous{{5, MakeState(5, "A", 5, false)}};
    std::map<int, ManagedChannelInputState> current{{5, MakeState(5, "", 0, false, false)}};
    auto decision = ClassifyChange(previous, current);
    Expect(decision.action == Action::WarnInvalidSource, "unreadable source should warn");
    ExpectChannels(decision.changed_channels, {5}, "unreadable source should identify the changed channel");
}

void TestMixedChangesPreferWarning() {
    std::map<int, ManagedChannelInputState> previous{
        {1, MakeState(1, "A", 1, false)},
        {2, MakeState(2, "A", 2, false)},
    };
    std::map<int, ManagedChannelInputState> current{
        {1, MakeState(1, "B", 7, false)},
        {2, MakeState(2, "A", 2, true)},
    };
    auto decision = ClassifyChange(previous, current);
    Expect(decision.action == Action::WarnTopologyChange, "topology warning should win over reroute");
    ExpectChannels(decision.changed_channels, {1, 2}, "mixed change set should report all changed channels");
}

void TestBootstrapDoesNotTrigger() {
    std::map<int, ManagedChannelInputState> previous;
    std::map<int, ManagedChannelInputState> current{{6, MakeState(6, "USB", 12, false)}};
    auto decision = ClassifyChange(previous, current);
    Expect(decision.action == Action::None, "bootstrap snapshot should not trigger reroute");
    Expect(decision.changed_channels.empty(), "bootstrap snapshot should not report changes");
}

} // namespace

int main() {
    TestNoChange();
    TestMonoSourceChangeReapplies();
    TestStereoSourceChangeReapplies();
    TestTopologyChangeWarns();
    TestInvalidSourceWarns();
    TestUnreadableSourceWarns();
    TestMixedChangesPreferWarning();
    TestBootstrapDoesNotTrigger();

    std::cout << "source_monitor_tests: OK\n";
    return 0;
}
