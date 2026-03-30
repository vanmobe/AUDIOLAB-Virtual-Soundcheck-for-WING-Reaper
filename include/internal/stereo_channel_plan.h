#ifndef WINGCONNECTOR_INTERNAL_STEREO_CHANNEL_PLAN_H
#define WINGCONNECTOR_INTERNAL_STEREO_CHANNEL_PLAN_H

namespace WingConnector::StereoChannelPlan {

constexpr int kMaxWingChannelCount = 40;

inline bool HasStereoPartner(int channel_number) {
    return channel_number > 0 && channel_number < kMaxWingChannelCount;
}

inline int PartnerChannel(int channel_number) {
    return HasStereoPartner(channel_number) ? (channel_number + 1) : 0;
}

}  // namespace WingConnector::StereoChannelPlan

#endif
