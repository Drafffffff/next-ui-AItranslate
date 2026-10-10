#!/usr/bin/env python3
"""Measure the Brick sidebar before allocating its panel windows."""
from pathlib import Path
import sys

path = Path(sys.argv[1]) / 'src/game.cpp'
text = path.read_text()
marker = '// Brick: measure current widget heights before allocating sidebar windows.'
if marker not in text:
    anchor = '    // Total up height used by all panels, and see what is left over for log\n'
    assert text.count(anchor) == 1, 'Pinned panel drawing source drifted'
    text = text.replace(anchor, '''    // Brick: measure current widget heights before allocating sidebar windows.
    // Layout updates dynamic factory heights. The first draw otherwise clips
    // limbs/needs using JSON defaults; reopening a menu or sleep then changes
    // the screen even though the character and game time have not changed.
    // No persistent text cache or additional panel/texture ownership is added.
    if( draw_this_turn && mgr.get_current_layout_id() == "brick_minimal_sidebar" ) {
        for( const window_panel &panel : mgr.get_current_layout().panels() ) {
            const widget_id &id = panel.get_widget();
            if( panel.toggle && panel.render() && id.is_valid() && panel.get_width() > 2 ) {
                widget measured = id.obj();
                measured.layout( u, panel.get_width() - 2, 0,
                                 measured.has_flag( "W_NO_PADDING" ) );
            }
        }
    }
''' + anchor, 1)
    assert '#include "worldfactory.h"' in text
    text = text.replace('#include "worldfactory.h"', '#include "widget.h"\n#include "worldfactory.h"', 1)
    path.write_text(text)
print('Applied Brick sidebar measurement before window allocation.')
