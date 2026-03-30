#ifndef SOUND_CHECK_STATE_H
#define SOUND_CHECK_STATE_H

#include <map>
#include <string>
#include <vector>

namespace WingConnector {
namespace SoundcheckState {

inline std::vector<std::string> BuildAltEnabledPaths(const std::vector<int>& channel_numbers) {
    std::vector<std::string> paths;
    paths.reserve(channel_numbers.size());
    for (int channel_number : channel_numbers) {
        paths.push_back("/ch/" + std::to_string(channel_number) + "/in/set/altsrc");
    }
    return paths;
}

inline std::vector<std::string> BuildAltGroupPaths(const std::vector<int>& channel_numbers) {
    std::vector<std::string> paths;
    paths.reserve(channel_numbers.size());
    for (int channel_number : channel_numbers) {
        paths.push_back("/ch/" + std::to_string(channel_number) + "/in/conn/altgrp");
    }
    return paths;
}

inline bool IsManagedSoundcheckActive(const std::vector<std::string>& alt_enabled_paths,
                                      const std::vector<std::string>& alt_group_paths,
                                      const std::map<std::string, int>& alt_enabled,
                                      const std::map<std::string, std::string>& alt_groups,
                                      const std::string& output_mode) {
    const bool card_mode = (output_mode == "CARD");
    for (size_t i = 0; i < alt_enabled_paths.size() && i < alt_group_paths.size(); ++i) {
        auto enabled_it = alt_enabled.find(alt_enabled_paths[i]);
        if (enabled_it == alt_enabled.end() || enabled_it->second == 0) {
            continue;
        }

        auto group_it = alt_groups.find(alt_group_paths[i]);
        const std::string group = (group_it != alt_groups.end()) ? group_it->second : std::string();
        const bool group_matches = card_mode
            ? (group == "CARD" || group == "CRD")
            : (group == "USB");
        if (group_matches) {
            return true;
        }
    }
    return false;
}

}  // namespace SoundcheckState
}  // namespace WingConnector

#endif  // SOUND_CHECK_STATE_H
