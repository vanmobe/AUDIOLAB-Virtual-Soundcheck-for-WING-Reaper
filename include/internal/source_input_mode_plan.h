#ifndef WINGCONNECTOR_INTERNAL_SOURCE_INPUT_MODE_PLAN_H
#define WINGCONNECTOR_INTERNAL_SOURCE_INPUT_MODE_PLAN_H

namespace WingConnector::SourceInputModePlan {

inline bool CanApplyStereoInPlace(int input_num) {
    return input_num > 0 && (input_num % 2) == 1;
}

inline int PlannedStereoPrimaryInputStart(int current_input_num, int playback_start) {
    if (playback_start > 0 && (playback_start % 2) == 1) {
        return playback_start;
    }
    return CanApplyStereoInPlace(current_input_num) ? current_input_num : 0;
}

}  // namespace WingConnector::SourceInputModePlan

#endif
