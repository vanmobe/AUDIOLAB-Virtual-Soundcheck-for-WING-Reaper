#ifndef TRACK_MANAGER_H
#define TRACK_MANAGER_H

#include "wing_osc.h"
#include "wing_config.h"
#include <vector>
#include <string>

// Forward declare Reaper types
class MediaTrack;
class ReaProject;

namespace WingConnector {

class TrackManager {
public:
    TrackManager(const WingConfig& config);
    ~TrackManager();
    
    // Create tracks from Wing channel data
    int CreateTracksFromChannelData(const std::map<int, ChannelInfo>& channels);
    
    // Create a single track
    MediaTrack* CreateTrack(int index, const std::string& name, int color_id);
    
    // Create stereo track
    MediaTrack* CreateStereoTrack(int index, const std::string& name, int color_id);
    
    // Update existing track from channel info
    void UpdateTrack(MediaTrack* track, const ChannelInfo& channel);
    
    // Find existing Wing tracks
    std::vector<MediaTrack*> FindExistingWingTracks();
    int ClearExistingWingTracks();
    
    // Clear all tracks (with user confirmation)
    void ClearAllTracks();
    
    // Track configuration
    void SetTrackColor(MediaTrack* track, int wing_color_id);
    bool TrackColorMatches(MediaTrack* track, int wing_color_id);
    void SetTrackName(MediaTrack* track, const std::string& name);
    void SetTrackInput(MediaTrack* track, int input_index, int num_channels = 1);
    bool SetTrackHardwareOutput(MediaTrack* track, int output_index, int num_channels = 1);
    void ClearTrackHardwareOutputs(MediaTrack* track);
    void ArmTrackForRecording(MediaTrack* track, bool arm = true);
    
private:
    // Wing color palette structure
    struct ColorRGB { uint8_t r, g, b; };
    
    // Static color lookup table (Wing console colors)
    static constexpr ColorRGB WING_COLORS[] = {
        { 68,  77, 112},  // 0
        { 50,  85, 207},  // 1
        {145, 116, 171},  // 2
        { 29,  97,  88},  // 3
        { 92, 181,  78},  // 4
        {141, 247, 146},  // 5
        {233, 240,  72},  // 6
        {171, 139, 116},  // 7
        {235,  53,  16},  // 8
        {235, 138, 162},  // 9
        {245, 113, 174},  // 10
        {213, 158, 240},  // 11
        {232, 211,  19},  // 12
        { 78, 160, 181},  // 13
        {207, 115,  50},  // 14
        {102, 186, 167},  // 15
        {115, 111, 112},  // 16
        {212, 209, 207},  // 17
    };
    static constexpr int NUM_WING_COLORS = sizeof(WING_COLORS) / sizeof(ColorRGB);
    
    const WingConfig& config_;
    std::vector<MediaTrack*> created_tracks_;
    
    // Color conversion
    int WingColorToReaperColor(int wing_color_id);
    int RGBToReaperColor(uint8_t r, uint8_t g, uint8_t b);
    
    // Get current Reaper project
    ReaProject* GetCurrentProject();
};

} // namespace WingConnector

#endif // TRACK_MANAGER_H
