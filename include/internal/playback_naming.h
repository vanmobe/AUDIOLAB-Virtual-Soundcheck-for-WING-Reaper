#ifndef WINGCONNECTOR_INTERNAL_PLAYBACK_NAMING_H
#define WINGCONNECTOR_INTERNAL_PLAYBACK_NAMING_H

#include <string>

namespace WingConnector::PlaybackNaming {

inline std::string StereoInputName(const std::string& base_name) {
    return base_name;
}

inline std::string StereoOutputLeftName(const std::string& base_name) {
    return base_name + " (L)";
}

inline std::string StereoOutputRightName(const std::string& base_name) {
    return base_name + " (R)";
}

}  // namespace WingConnector::PlaybackNaming

#endif
