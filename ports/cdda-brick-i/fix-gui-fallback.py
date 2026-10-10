#!/usr/bin/env python3
"""Render on-demand Chinese once, with its own clip rather than every draw command."""
from pathlib import Path
import sys

source = Path(sys.argv[1])
p = source / 'src/third-party/imgui/imgui_draw.cpp'
t = p.read_text()
if '// Brick: retain the original fallback clip.' not in t:
    anchor = '                    draw_list->FallbackGlyphs.push_back(glyphToDraw);'
    assert t.count(anchor) == 1
    t = t.replace(anchor, '''                    // Brick: retain the original fallback clip.
                    if (std::getenv("CDDA_BRICK")) {
                        brick_gui_fallback::record(draw_list, clip_rect);
                    }
''' + anchor, 1)
    anchor = '#include "imgui_internal.h"'
    assert t.count(anchor) == 1
    t = t.replace(anchor, anchor + '\n#include <cstdlib>\n#include "../../brick_gui_fallback.h"', 1)
    p.write_text(t)

p = source / 'src/third-party/imgui/imgui_impl_sdlrenderer2.cpp'
t = p.read_text()
if '// Brick: draw fallback text once after the window geometry.' not in t:
    anchor = '#include "imgui_impl_sdlrenderer2.h"'
    assert t.count(anchor) == 1
    t = t.replace(anchor, anchor + '\n#include <cstdlib>\n#include "../../brick_gui_fallback.h"', 1)
    t = t.replace('            if(drawFallbackGlyphCallback)',
                  '            if(drawFallbackGlyphCallback && !std::getenv("CDDA_BRICK"))', 1)
    anchor = '    platform_io.Renderer_RenderState = NULL;'
    at = t.index(anchor)
    # The closing braces just before this point finish command and list loops.
    old = '''        }
    }
'''
    assert t[:at].endswith(old)
    insert = '''        }
        // Brick: draw fallback text once after the window geometry.
        if (drawFallbackGlyphCallback && std::getenv("CDDA_BRICK")) {
            const auto found = brick_gui_fallback::clips.find(draw_list);
            const bool matched = found != brick_gui_fallback::clips.end() &&
                                 found->second.size() == size_t(draw_list->FallbackGlyphs.Size);
            if (draw_list->FallbackGlyphs.Size && !matched && std::getenv("CDDA_BRICK_VERIFY_GUI")) {
                std::abort();
            }
            for (int i = 0; i < draw_list->FallbackGlyphs.Size; ++i) {
                if (matched) {
                    const ImVec4 &clip = found->second[i];
                    const float x0 = std::max(0.f, (clip.x - clip_off.x) * clip_scale.x);
                    const float y0 = std::max(0.f, (clip.y - clip_off.y) * clip_scale.y);
                    const float x1 = std::min(float(fb_width), (clip.z - clip_off.x) * clip_scale.x);
                    const float y1 = std::min(float(fb_height), (clip.w - clip_off.y) * clip_scale.y);
                    if (x1 <= x0 || y1 <= y0) { continue; }
                    SDL_Rect r = {int(x0), int(y0), int(x1-x0), int(y1-y0)};
                    SDL_RenderSetClipRect(renderer, &r);
                } else {
                    SDL_RenderSetClipRect(renderer, nullptr);
                }
                drawFallbackGlyphCallback(draw_list->FallbackGlyphs[i]);
            }
        }
    }
'''
    t = t[:at-len(old)] + insert + t[at:]
    p.write_text(t)
p = source / 'src/cata_imgui.cpp'
t = p.read_text()
if '// Brick: GUI theme colors need a visible palette fallback.' not in t:
    anchor = '''            if( it != sdlColorsToCata.end() ) {
                col = it->second;
            }'''
    new = anchor + ''' else if( std::getenv( "CDDA_BRICK" ) ) {
                // Brick: GUI theme colors need a visible palette fallback.
                unsigned nearest = 3 * 255 * 255 + 1;
                for( const auto &entry : sdlColorsToCata ) {
                    unsigned distance = 0;
                    for( unsigned shift : { 0u, 8u, 16u } ) {
                        const int difference = int( ( glyph.col >> shift ) & 255 ) -
                                               int( ( entry.first >> shift ) & 255 );
                        distance += difference * difference;
                    }
                    if( distance < nearest ) {
                        nearest = distance;
                        col = entry.second;
                    }
                }
            }'''
    assert t.count(anchor) == 1
    p.write_text(t.replace(anchor, new, 1))
print('Applied original clipping, single-pass rendering and theme-safe Chinese colors.')
