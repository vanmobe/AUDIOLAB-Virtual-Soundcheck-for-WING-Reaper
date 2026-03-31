/*
 * Shared adoption editor types for native platform dialogs.
 */

#ifndef WINGCONNECTOR_INTERNAL_ADOPTION_EDITOR_H
#define WINGCONNECTOR_INTERNAL_ADOPTION_EDITOR_H

#include <string>
#include <vector>

struct AdoptionEditorRow {
    int track_index = 0;
    std::string track_name;
    bool stereo_like = false;
    int suggested_channel = 0;
    int assigned_channel = 0;
    int suggested_slot_start = 0;
    int suggested_slot_end = 0;
};

#endif
