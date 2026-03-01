#ifndef TRACK_MANAGER_H
#define TRACK_MANAGER_H

#include "wing_osc.h"
#include "wing_config.h"
#include <vector>
#include <string>

// Forward declare Reaper types
struct MediaTrack;
struct ReaProject;

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
    
    // Clear all tracks (with user confirmation)
    void ClearAllTracks();
    
    // Track configuration
    void SetTrackColor(MediaTrack* track, int wing_color_id);
    void SetTrackName(MediaTrack* track, const std::string& name);
    void SetTrackInput(MediaTrack* track, int input_index, int num_channels = 1);
    bool SetTrackHardwareOutput(MediaTrack* track, int output_index, int num_channels = 1);
    void ClearTrackHardwareOutputs(MediaTrack* track);
    void ArmTrackForRecording(MediaTrack* track, bool arm = true);
    
private:
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
