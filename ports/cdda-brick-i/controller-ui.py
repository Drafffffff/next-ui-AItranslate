#!/usr/bin/env python3
"""Hide keyboard decorations in the pinned I UI, retaining all input bindings."""
from pathlib import Path
import shutil
import sys

source = Path(sys.argv[1]) / 'src'
port = Path(__file__).resolve().parent
dst = source / 'brick_ui.h'
if not dst.exists() or dst.read_bytes() != (port / dst.name).read_bytes():
    shutil.copyfile(port / dst.name, dst)


def patch(name, marker, old, new):
    path = source / name
    text = path.read_text()
    if marker in text:
        return
    assert text.count(old) == 1, (name, marker, text.count(old))
    text = text.replace(old, new, 1)
    if '#include "brick_ui.h"' not in text:
        text = '#include "brick_ui.h"\n' + text
    path.write_text(text)


def wrap(name, marker, start, end):
    path = source / name
    text = path.read_text()
    if marker in text:
        return
    a = text.index(start)
    b = text.index(end, a) + len(end)
    old = text[a:b]
    patch(name, marker, old, '// ' + marker + '\n    if( !brick_ui::controller_hints_only() ) {\n' + old + '\n    }')


patch('input_context.cpp', 'Brick: action labels without keyboard decorations',
      '    if( action_descriptor == "ANY_INPUT" ) {\n        //~ keybinding description for anykey',
      '    // Brick: action labels without keyboard decorations\n'
      '    if( brick_ui::controller_hints_only() ) { return text; }\n'
      '    if( action_descriptor == "ANY_INPUT" ) {\n        //~ keybinding description for anykey')

patch('output.cpp', 'Brick: plain menu mnemonic text',
      '        return prestring + colorize( shortcut, shortcut_color ) + poststring;',
      '        // Brick: plain menu mnemonic text\n'
      '        return prestring + ( brick_ui::controller_hints_only() ? shortcut :\n'
      '                             colorize( shortcut, shortcut_color ) ) + poststring;')

patch('uilist.cpp', 'Brick: hide entry keyboard labels',
      '                        if( entry.hotkey.has_value() ) {',
      '                        // Brick: hide entry keyboard labels\n'
      '                        if( !brick_ui::controller_hints_only() && entry.hotkey.has_value() ) {')
patch('inventory_ui.cpp', 'Brick: hide item keyboard letters',
      "            if( entry.get_invlet() != '\\0' ) {",
      "            // Brick: hide item keyboard letters\n"
      "            if( !brick_ui::controller_hints_only() && entry.get_invlet() != '\\0' ) {")
patch('inventory_ui.cpp', 'Brick: keep filter value without shortcut',
      '            filter_offset = utf8_width( text + filter ) + 6;',
      '            // Brick: keep filter value without shortcut\n'
      '            if( brick_ui::controller_hints_only() ) {\n'
      '                text = filter.empty() ? "" : _( "Filter: " );\n'
      '            }\n'
      '            filter_offset = utf8_width( text + filter ) + 6;')
wrap('inventory_ui.cpp', 'Brick: category mode label belongs in controller panel',
     '        right_print( w, getmaxy( w ) - border, border + 1, c_light_gray,',
     '                                    io::enum_to_string( _uimode ) ) );')
wrap('inventory_ui.cpp', 'Brick: pickup keyboard instructions are redundant',
     '    set_hint( string_format(\n                  _( "%s wield %s wear',
     '                  colorize( ctxt.get_desc( "TOGGLE_ENTRY" ), c_yellow ) ) );')
patch('inventory_ui.cpp', 'Brick: unload mode is state rather than shortcut text',
      '    std::string mode = uistate.unload_auto_contain ? _( "Auto" ) : _( "Manual" );',
      '    std::string mode = uistate.unload_auto_contain ? _( "Auto" ) : _( "Manual" );\n'
      '    // Brick: unload mode is state rather than shortcut text\n'
      '    if( brick_ui::controller_hints_only() ) { return mode; }')

patch('crafting_gui.cpp', 'Brick: controller strip replaces crafting keyboard footer',
      '        const int tailHeight = keybinding_tips.size() + 2;',
      '        // Brick: controller strip replaces crafting keyboard footer\n'
      '        if( brick_ui::controller_hints_only() ) { keybinding_tips.clear(); }\n'
      '        const int tailHeight = keybinding_tips.size() + 2;')

wrap('overmap_ui.cpp', 'Brick: hide overmap keyboard command legend',
     '    const auto print_hint = [&]( const std::string & action, nc_color color = c_magenta ) {',
     '    print_hint( "QUIT" );')

patch('advanced_inv.cpp', 'Brick: page count without keyboard arrows',
      '        mvwprintz( w, point( 2, 4 ), c_light_blue, _( "[<] page %1$d of %2$d [>]" ), cur_page + 1,',
      '        // Brick: page count without keyboard arrows\n'
      '        mvwprintz( w, point( 2, 4 ), c_light_blue, brick_ui::controller_hints_only() ?\n'
      '                   _( "Page %1$d of %2$d" ) : _( "[<] page %1$d of %2$d [>]" ), cur_page + 1,')
patch('advanced_inv.cpp', 'Brick: sort state without keyboard label',
      '    mvwprintw( w, point( 3, 0 ), _( "< [%s] Sort: %s >" ), ctxt.get_desc( "SORT" ),\n'
      '               get_sortname( pane.sortby ) );',
      '    // Brick: sort state without keyboard label\n'
      '    if( brick_ui::controller_hints_only() ) {\n'
      '        mvwprintw( w, point( 3, 0 ), "< %s >", get_sortname( pane.sortby ) );\n'
      '    } else {\n'
      '        mvwprintw( w, point( 3, 0 ), _( "< [%s] Sort: %s >" ), ctxt.get_desc( "SORT" ),\n'
      '                   get_sortname( pane.sortby ) );\n'
      '    }')
patch('advanced_inv.cpp', 'Brick: plain active filter label',
      '    std::string fprefix = string_format( _( "[%s] Filter" ), ctxt.get_desc( "FILTER" ) );',
      '    // Brick: plain active filter label\n'
      '    std::string fprefix = brick_ui::controller_hints_only() ? _( "Filter" ) :\n'
      '                          string_format( _( "[%s] Filter" ), ctxt.get_desc( "FILTER" ) );')
patch('advanced_inv.cpp', 'Brick: filter reset is in controller panel',
      '    if( !filter_edit && !filter.empty() ) {',
      '    // Brick: filter reset is in controller panel\n'
      '    if( !brick_ui::controller_hints_only() && !filter_edit && !filter.empty() ) {')
wrap('advanced_inv.cpp', 'Brick: hide advanced inventory keyboard legend',
     '    right_print( head, 0, +3, c_white, string_format(',
     '                     ctxt.get_desc( "HELP_KEYBINDINGS" ) ) );')
patch('input_context.cpp', 'Brick: GUI buttons use plain action labels',
      '    if( action_descriptor == "ANY_INPUT" ) {\n        return ""; // what sort of crazy button would this be?',
      '    // Brick: GUI buttons use plain action labels\n'
      '    if( brick_ui::controller_hints_only() ) { return action_text; }\n'
      '    if( action_descriptor == "ANY_INPUT" ) {\n        return ""; // what sort of crazy button would this be?')
patch('advanced_inv.cpp', 'Brick: location keypad legend is redundant',
      '    int wwidth = getmaxx( window );',
      '    int wwidth = getmaxx( window );\n'
      '    // Brick: location keypad legend is redundant\n'
      '    if( brick_ui::controller_hints_only() ) { return wwidth; }')
patch('inventory_ui.cpp', 'Brick: no empty keyboard filter prompt',
      '        if( has_available_choices() || !filter.empty() ) {',
      '        // Brick: no empty keyboard filter prompt\n'
      '        if( ( has_available_choices() && !brick_ui::controller_hints_only() ) || !filter.empty() ) {')

print('Applied controller-only menu presentation.')

# Feed actual UI state to the controller overlay, including persisted/keyboard changes.
patch('crafting_gui.cpp', 'Brick: expose visible crafting panes',
      '        isWide = ( TERMX > FULL_SCREEN_WIDTH && freeWidth > 15 );',
      '        isWide = ( TERMX > FULL_SCREEN_WIDTH && freeWidth > 15 );\n'
      '        // Brick: expose visible crafting panes\n'
      '#if defined(TILES)\n'
      '        brick_input::set_crafting_view( isWide );\n'
      '#endif')
patch('overmap_ui.cpp', 'Brick: actual overmap cursor mode in controller hints',
      '    do {\n        ui_manager::redraw();\n#if (defined TILES || defined _WIN32 || defined WINDOWS )',
      '    do {\n        // Brick: actual overmap cursor mode in controller hints\n'
      '#if defined(TILES)\n'
      '        brick_input::set_overmap_cursor_mode( uistate.overmap_fast_scroll, fast_scroll_offset );\n'
      '#endif\n'
      '        ui_manager::redraw();\n#if (defined TILES || defined _WIN32 || defined WINDOWS )')
for name in ('crafting_gui.cpp', 'overmap_ui.cpp'):
    path = source / name
    text = path.read_text()
    if '#include "brick_input.h"' not in text:
        path.write_text('#if defined(TILES)\n#include "brick_input.h"\n#endif\n' + text)
