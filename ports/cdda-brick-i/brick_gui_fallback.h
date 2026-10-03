#pragma once
#include <unordered_map>
#include <vector>

// Sidecar metadata keeps the bundled ImGui public structures unchanged.
// A fallback glyph belongs to the clip rectangle active when text was built.
namespace brick_gui_fallback
{
inline int frame = -1;
inline std::unordered_map<const ImDrawList *, std::vector<ImVec4>> clips;
inline void record( const ImDrawList *list, const ImVec4 &clip )
{
    if( frame != ImGui::GetFrameCount() ) {
        clips.clear();
        frame = ImGui::GetFrameCount();
    }
    clips[list].push_back( clip );
}
}
