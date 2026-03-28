#ifndef MANAGED_SOURCE_MONITOR_H
#define MANAGED_SOURCE_MONITOR_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include "wingconnector/wing_osc.h"

namespace WingConnector {
namespace ManagedSourceMonitor {

enum class Action {
    None,
    ReapplyRouting,
    WarnTopologyChange,
    WarnInvalidSource,
};

struct Decision {
    Action action = Action::None;
    std::vector<int> changed_channels;
};

inline bool IsValidState(const ManagedChannelInputState& state) {
    return state.readable && !state.source_group.empty() && state.source_group != "OFF" && state.source_input > 0;
}

inline Decision ClassifyChange(const std::map<int, ManagedChannelInputState>& previous,
                               const std::map<int, ManagedChannelInputState>& current) {
    Decision decision;
    std::set<int> changed;

    for (const auto& [channel_number, previous_state] : previous) {
        auto current_it = current.find(channel_number);
        if (current_it == current.end()) {
            changed.insert(channel_number);
            decision.action = Action::WarnInvalidSource;
            continue;
        }

        const ManagedChannelInputState& current_state = current_it->second;
        const bool previous_valid = IsValidState(previous_state);
        const bool current_valid = IsValidState(current_state);

        if (!current_valid) {
            if (previous_valid ||
                previous_state.readable != current_state.readable ||
                previous_state.source_group != current_state.source_group ||
                previous_state.source_input != current_state.source_input) {
                changed.insert(channel_number);
                decision.action = Action::WarnInvalidSource;
            }
            continue;
        }

        if (!previous_valid) {
            changed.insert(channel_number);
            if (decision.action != Action::WarnInvalidSource) {
                decision.action = Action::ReapplyRouting;
            }
            continue;
        }

        if (previous_state.stereo_linked != current_state.stereo_linked) {
            changed.insert(channel_number);
            decision.action = Action::WarnTopologyChange;
            continue;
        }

        if (previous_state.source_group != current_state.source_group ||
            previous_state.source_input != current_state.source_input) {
            changed.insert(channel_number);
            if (decision.action == Action::None) {
                decision.action = Action::ReapplyRouting;
            }
        }
    }

    decision.changed_channels.assign(changed.begin(), changed.end());
    return decision;
}

}  // namespace ManagedSourceMonitor
}  // namespace WingConnector

#endif  // MANAGED_SOURCE_MONITOR_H
