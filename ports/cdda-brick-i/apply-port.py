#!/usr/bin/env python3
"""Apply only the preserved CDDA I controller/UI layer to pristine pinned source.

No startup pack, pressure governor, data compaction, cold reload or save codec.
This is the UI migration base, not a Steam Deck controller/backend release.
"""
from pathlib import Path
import shutil
import subprocess
import sys

source = Path(sys.argv[1])
port = Path(__file__).resolve().parent
for name in ('brick_input.cpp', 'brick_input.h', 'brick_gui.h', 'brick_gui_fallback.h'):
    destination = source / 'src' / name
    if not destination.exists() or destination.read_bytes() != (port / name).read_bytes():
        shutil.copyfile(port / name, destination)

# UI block extracted from archived apply-port.py:14-14
path = source / 'src/sdltiles.cpp'

# UI block extracted from archived apply-port.py:15-15
text = path.read_text()

# UI block extracted from archived apply-port.py:16-32
if '#include "brick_input.h"' not in text:
    changes = {
        '#include "sdl_gamepad.h"': '#include "sdl_gamepad.h"\n#include "brick_input.h"',
        '    if( get_option<bool>( "ENABLE_JOYSTICK" ) ) {':
            '    brick_input::init();\n    if( get_option<bool>( "ENABLE_JOYSTICK" ) ) {',
        '    gamepad::quit();': '    brick_input::shutdown();\n    gamepad::quit();',
        '    SDL_RenderPresent( renderer.get() );':
            '    brick_input::render_overlay( renderer.get() );\n    SDL_RenderPresent( renderer.get() );',
    }
    for old, new in changes.items():
        if text.count(old) != 1:
            raise SystemExit(f'Unexpected pinned SDL source: {old!r}')
        text = text.replace(old, new)
    if text.count('SDL_PollEvent( &ev )') != 2:
        raise SystemExit('Unexpected pinned SDL event loops')
    text = text.replace('SDL_PollEvent( &ev )', 'brick_input::poll_event( &ev )')
    path.write_text(text)

# UI block extracted from archived apply-port.py:137-137
path = source / 'src/font_loader.cpp'

# UI block extracted from archived apply-port.py:138-138
text = path.read_text()

# UI block extracted from archived apply-port.py:139-153
if '// Brick: fixed-width English and Chinese fonts' not in text:
    text = '#include <cstdio>\n#include <cstdlib>\n' + text
    old = '        load_font_from_config( config, "overmap_typeface", overmap_typeface );'
    new = old + '''
        // Brick: fixed-width English and Chinese fonts, including the new GUI.
        const char *brick = std::getenv( "CDDA_BRICK" );
        if( brick && std::string( brick ) == "1" ) {
            const std::vector<font_config> mono = { font_config( "data/font/Terminus.ttf" ),
                                                   font_config( "data/font/unifont.ttf" ) };
            typeface = map_typeface = overmap_typeface = gui_typeface = mono;
            std::fprintf( stderr, "Brick fixed-width fonts: Terminus ASCII + Unifont Chinese\\n" );
        }
'''
    if text.count(old) != 1: raise SystemExit('Unexpected I font configuration')
    path.write_text(text.replace(old,new,1))

# UI block extracted from archived apply-port.py:156-156
path = source / 'src/sdltiles.cpp'

# UI block extracted from archived apply-port.py:157-157
text = path.read_text()

# UI block extracted from archived apply-port.py:158-177
if '// Brick: reserve the hint strip' not in text:
    changes = {
        '    WindowHeight = TERMINAL_HEIGHT * fontheight * scaling_factor;':
            '    // Brick: reserve the hint strip below the terminal.\n    WindowHeight = TERMINAL_HEIGHT * fontheight * scaling_factor + brick_input::reserved_height();',
        'SDL_TEXTUREACCESS_TARGET, WindowWidth / scaling_factor, WindowHeight / scaling_factor )':
            'SDL_TEXTUREACCESS_TARGET, WindowWidth / scaling_factor, ( WindowHeight - brick_input::reserved_height() ) / scaling_factor )',
        '        TERMINAL_HEIGHT = WindowHeight / fontheight / scaling_factor;':
            '        TERMINAL_HEIGHT = ( WindowHeight - brick_input::reserved_height() ) / fontheight / scaling_factor;',
        '    TERMINAL_HEIGHT = std::max( WindowHeight / fontheight / scaling_factor, EVEN_MINIMUM_TERM_HEIGHT );':
            '    TERMINAL_HEIGHT = std::max( ( WindowHeight - brick_input::reserved_height() ) / fontheight / scaling_factor, EVEN_MINIMUM_TERM_HEIGHT );',
        '    RenderCopy( renderer, display_buffer, nullptr, nullptr );':
            '    SDL_Rect game_rect{ 0, 0, WindowWidth, WindowHeight - brick_input::reserved_height() };\n    RenderCopy( renderer, display_buffer, nullptr, &game_rect );',
        '    int h = cell_h * fontheight * scaling_factor;':
            '    int h = cell_h * fontheight * scaling_factor + brick_input::reserved_height();',
    }
    for old, new in changes.items():
        if text.count(old) != 1:
            raise SystemExit(f'Unexpected pinned viewport source: {old!r}')
        text = text.replace(old, new)
    path.write_text(text)

# UI block extracted from archived apply-port.py:179-179
path = source / 'src/input_context.cpp'

# UI block extracted from archived apply-port.py:180-180
text = path.read_text()

# UI block extracted from archived apply-port.py:181-187
if 'brick_input::set_context( category' not in text:
    text = text.replace('#include "input.h"', '#include "input.h"\n#if defined(TILES)\n#include "brick_input.h"\n#endif', 1)
    old = 'const std::string &input_context::handle_input( const int timeout )\n{'
    if text.count(old) != 1:
        raise SystemExit('Unexpected pinned input context')
    text = text.replace(old, old + '\n#if defined(TILES)\n    brick_input::set_context( category );\n#endif', 1)
    path.write_text(text)

# UI block extracted from archived apply-port.py:191-191
path = source / 'src/input_context.cpp'

# UI block extracted from archived apply-port.py:192-192
text = path.read_text()

# UI block extracted from archived apply-port.py:193-194
text = text.replace('brick_input::set_context( category );',
                    'brick_input::set_context( category, registered_actions );')

# UI block extracted from archived apply-port.py:195-219
if 'Brick: resolve only actions registered by the current menu' not in text:
    old = 'const std::string &input_context::input_to_action( const input_event &inp ) const\n{'
    new = old + '''
#if defined(TILES)
    // Brick: resolve only actions registered by the current menu.
    const std::string &brick_action = brick_input::mapped_action( inp.get_first_input() );
    if( !brick_action.empty() ) {
        const auto found = std::find( registered_actions.begin(), registered_actions.end(), brick_action );
        return found != registered_actions.end() ? *found : CATA_ERROR;
    }
#endif'''
    if text.count(old) != 1:
        raise SystemExit('Unexpected pinned logical input action hook')
    text = text.replace(old, new, 1)
    old = '        const std::string &action = input_to_action( next_action );'
    new = old + '''
#if defined(TILES)
        // A queued command from a closed menu must never become ANY_INPUT.
        if( action == CATA_ERROR && !brick_input::mapped_action( next_action.get_first_input() ).empty() ) {
            continue;
        }
#endif'''
    if text.count(old) != 1:
        raise SystemExit('Unexpected pinned stale controller action hook')
    text = text.replace(old, new, 1)

# UI block extracted from archived apply-port.py:220-220
path.write_text(text)

# UI block extracted from archived apply-port.py:222-222
path = source / 'src/sdltiles.cpp'

# UI block extracted from archived apply-port.py:223-223
text = path.read_text()

# UI block extracted from archived apply-port.py:224-237
if 'Brick: private SDL command events are not text input' not in text:
    old = '    while( brick_input::poll_event( &ev ) ) {\n        imclient->process_input( &ev );\n        switch( ev.type ) {'
    new = '''    while( brick_input::poll_event( &ev ) ) {
        // Brick: private SDL command events are not text input.
        const int brick_code = brick_input::event_action_code( ev );
        if( brick_code ) {
            last_input = input_event( brick_code, input_event_t::keyboard_char );
            break; // Deliver each command once, preserving any following events.
        }
        imclient->process_input( &ev );
        switch( ev.type ) {'''
    if text.count(old) != 1:
        raise SystemExit('Unexpected pinned logical SDL event hook')
    text = text.replace(old, new, 1)

# UI block extracted from archived apply-port.py:238-238
path.write_text(text)

# UI block extracted from archived apply-port.py:251-251
path = source / 'src/sdl_font.cpp'

# UI block extracted from archived apply-port.py:252-252
text = path.read_text()

# UI block extracted from archived apply-port.py:253-287
if 'Brick: preserve complete glyphs in the condensed large-text grid' not in text:
    text = text.replace('#include "sdl_font.h"', '#include "sdl_font.h"\n#include <cstdlib>\n#include <cstring>', 1)
    old = '    TTF_SetFontStyle( font.get(), TTF_STYLE_NORMAL );'
    if text.count(old) != 1:
        raise SystemExit('Unexpected pinned font style')
    text = text.replace(old, '''    const char *brick = std::getenv( "CDDA_BRICK" );
    TTF_SetFontStyle( font.get(), brick && std::strcmp( brick, "1" ) == 0 && fontsize >= 28 ?
                      TTF_STYLE_BOLD : TTF_STYLE_NORMAL );''', 1)
    old = '''    const int wf = utf8_width( ch );
    ch_width = width * wf;
    // Note: bits per pixel'''
    if text.count(old) != 1:
        raise SystemExit('Unexpected pinned glyph renderer')
    text = text.replace(old, '''    // Brick: preserve complete glyphs in the condensed large-text grid.
    const char *brick = std::getenv( "CDDA_BRICK" );
    const bool large_text = brick && std::strcmp( brick, "1" ) == 0 && height >= 28;
    if( large_text ) {
        SDL_Surface_Ptr rgba( SDL_ConvertSurfaceFormat( sglyph.get(), SDL_PIXELFORMAT_ARGB8888, 0 ) );
        if( rgba ) { sglyph = std::move( rgba ); }
    }
    const int wf = utf8_width( ch );
    ch_width = width * wf;
    // Note: bits per pixel''', 1)
    # The existing renderer centers and crops. Large text instead scales just
    # the glyph surface to the same fixed cell; menus keep their column count.
    old = '    SDL_Rect dst_rect = { 0, 0, ch_width, height };'
    text = text.replace(old, old + '''
    if( large_text ) {
        SDL_SetSurfaceBlendMode( sglyph.get(), SDL_BLENDMODE_NONE );
        if( SDL_BlitScaled( sglyph.get(), &src_rect, surface.get(), &dst_rect ) == 0 ) {
            SDL_SetSurfaceBlendMode( surface.get(), SDL_BLENDMODE_BLEND );
            return CreateTextureFromSurface( renderer, surface );
        }
    }''', 1)
    path.write_text(text)

# UI block extracted from archived apply-port.py:288-288
print('Applied complete-glyph large text rendering.')

# UI block extracted from archived apply-port.py:476-476
path = source / 'src/sdltiles.cpp'

# UI block extracted from archived apply-port.py:477-477
text = path.read_text()

# UI block extracted from archived apply-port.py:478-491
if '// Brick: wait for events rather than polling every millisecond.' not in text:
    start = text.index('    if( inputdelay < 0 ) {', text.index('input_event input_manager::get_input_event'))
    end = text.index('    SDL_GetMouseState(', start)
    block = text[start:end]
    if block.count('            SDL_Delay( 1 );') != 2:
        raise SystemExit('Unexpected pinned input waiting loops')
    block = block.replace('            SDL_Delay( 1 );', '            // Brick: wait for events rather than polling every millisecond.\n            brick_input::wait_for_event( 25 );', 1)
    old = '            SDL_Delay( 1 );\n            timedout = endtime >= starttime + inputdelay;'
    new = '            const uint32_t elapsed = endtime - starttime;\n            timedout = elapsed >= static_cast<uint32_t>( inputdelay );\n            if( !timedout ) {\n                brick_input::wait_for_event( std::min<uint32_t>( 25, inputdelay - elapsed ) );\n            }'
    if block.count(old) != 1:
        raise SystemExit('Unexpected timed input wait')
    block = block.replace(old, new, 1)
    text = text[:start] + '    brick_input::begin_input_wait();\n' + block + text[end:]
    path.write_text(text)

# UI block extracted from archived apply-port.py:492-492
path = source / 'src/input_context.cpp'

# UI block extracted from archived apply-port.py:493-493
text = path.read_text()

# UI block extracted from archived apply-port.py:494-499
if 'brick_input::note_action( *result );' not in text:
    old = '    inp_mngr.set_timeout( old_timeout );\n    return *result;'
    new = '    inp_mngr.set_timeout( old_timeout );\n#if defined(TILES)\n    brick_input::note_action( *result );\n#endif\n    return *result;'
    if text.count(old) != 1:
        raise SystemExit('Unexpected input action return')
    path.write_text(text.replace(old, new, 1))

# UI block extracted from archived apply-port.py:500-500
print('Applied event waiting and bounded menu performance diagnostics.')

# UI block extracted from archived apply-port.py:607-607
path = source / 'src/cata_imgui.cpp'

# UI block extracted from archived apply-port.py:608-608
text = path.read_text()

# UI block extracted from archived apply-port.py:609-627
if '// Brick: fixed advances for GUI ASCII and CJK glyphs.' not in text:
    anchor = '        io.Fonts->Build();'
    insert = '''        // Brick: fixed advances for GUI ASCII and CJK glyphs.
        if( std::getenv( "CDDA_BRICK" ) ) {
            for( ImFont *font : io.Fonts->Fonts ) {
                for( ImFontGlyph &glyph : font->Glyphs ) {
                    const int cells = utf8_width( utf32_to_utf8( glyph.Codepoint ) );
                    const float advance = fontwidth * cells;
                    const float factor = glyph.AdvanceX > 0 ? advance / glyph.AdvanceX : 1;
                    glyph.X0 *= factor;
                    glyph.X1 *= factor;
                    glyph.AdvanceX = advance;
                }
                font->BuildLookupTable();
            }
        }
'''
    assert text.count(anchor) == 1
    path.write_text(text.replace(anchor, anchor + '\n' + insert, 1))

# UI block extracted from archived apply-port.py:630-630
path = source / 'src/input_popup.cpp'

# UI block extracted from archived apply-port.py:631-631
text = path.read_text()

# UI block extracted from archived apply-port.py:632-646
if '// Brick: keep keyboard editing shortcuts in the new GUI.' not in text:
    anchor = '    ctxt.register_action( "TEXT.CLEAR" );'
    new = anchor + '''
    // Brick: keep keyboard editing shortcuts in the new GUI.
    if( std::getenv( "CDDA_BRICK" ) ) {
        ctxt.register_action( "TEXT.LEFT" );
        ctxt.register_action( "TEXT.RIGHT" );
        ctxt.register_action( "TEXT.BACKSPACE" );
    }'''
    assert text.count(anchor) == 1
    text = '#include <cstdlib>\n' + text.replace(anchor, new, 1)
    anchor = '    return next_loop;'
    new = '    // Brick: route virtual editing commands to both string and number widgets.\n    if( !next_loop && std::getenv( "CDDA_BRICK" ) ) {\n        ImGuiIO &io = ImGui::GetIO();\n        const ImGuiKey key = action == "TEXT.LEFT" ? ImGuiKey_LeftArrow :\n                             action == "TEXT.RIGHT" ? ImGuiKey_RightArrow :\n                             action == "TEXT.BACKSPACE" ? ImGuiKey_Backspace : ImGuiKey_None;\n        if( key != ImGuiKey_None ) {\n            io.AddKeyEvent( key, true );\n            io.AddKeyEvent( key, false );\n            return true;\n        }\n        if( action == "TEXT.CLEAR" ) {\n            io.AddKeyEvent( ImGuiMod_Ctrl, true );\n            io.AddKeyEvent( ImGuiKey_A, true );\n            io.AddKeyEvent( ImGuiKey_A, false );\n            io.AddKeyEvent( ImGuiKey_Backspace, true );\n            io.AddKeyEvent( ImGuiKey_Backspace, false );\n            io.AddKeyEvent( ImGuiMod_Ctrl, false );\n            return true;\n        }\n    }\n' + anchor
    assert text.count(anchor) == 1
    path.write_text(text.replace(anchor, new, 1))

# UI block extracted from archived apply-port.py:649-649
path = source / 'src/cata_imgui.cpp'

# UI block extracted from archived apply-port.py:650-650
text = path.read_text()

# UI block extracted from archived apply-port.py:651-659
if '// Brick: the bottom hint strip stays outside GUI content.' not in text:
    anchor = '    ImGui_ImplSDL2_NewFrame();'
    new = anchor + '''
    // Brick: the bottom hint strip stays outside GUI content.
    if( std::getenv( "CDDA_BRICK" ) ) {
        ImGui::GetIO().DisplaySize.y = std::max( 1.f, ImGui::GetIO().DisplaySize.y - 48.f );
    }'''
    assert text.count(anchor) == 1
    path.write_text(text.replace(anchor, new, 1))

# UI block extracted from archived apply-port.py:673-673
path = source / 'src/cata_imgui.cpp'

# UI block extracted from archived apply-port.py:674-674
text = path.read_text()

# UI block extracted from archived apply-port.py:675-691
if '// Brick: verify actual GUI measurements.' not in text:
    anchor = '        check_font( io.Fonts->Fonts[0] );'
    new = '''        // Brick: verify actual GUI measurements.
        if( std::getenv( "CDDA_BRICK_VERIFY_FONTS" ) ) {
            for( ImFont *font : io.Fonts->Fonts ) {
                const float latin = font->CalcTextSizeA( fontheight, 10000.f, 0.f, "iW0?" ).x;
                const float chinese = font->CalcTextSizeA( fontheight, 10000.f, 0.f, "中文制作" ).x;
                if( latin != 4 * fontwidth || chinese != 8 * fontwidth ) {
                    std::fprintf( stderr, "Brick GUI grid failure: Latin=%f Chinese=%f\\n", latin, chinese );
                    std::abort();
                }
                std::fprintf( stderr, "Brick GUI grid verified: Latin=%f Chinese=%f\\n", latin, chinese );
            }
        }
''' + anchor
    assert text.count(anchor) == 1
    path.write_text(text.replace(anchor, new, 1))

# UI block extracted from archived apply-port.py:780-780
path = source / 'src/input_popup.cpp'

# UI block extracted from archived apply-port.py:781-781
text = path.read_text()

# UI block extracted from archived apply-port.py:782-799
if '// Brick: GUI integration diagnostic.' not in text:
    text = '#include <cstdio>\n' + text
    anchor = '    ImGui::InputText( input_label.c_str(), &text, flags, input_callback, this );'
    insert = '''
    // Brick: GUI integration diagnostic.
    if( std::getenv( "CDDA_BRICK_VERIFY_GUI" ) ) {
        static size_t previous = static_cast<size_t>( -1 );
        static int previous_state = -1;
        const int state = ImGui::IsItemActive() + 2 * ImGui::GetIO().KeyCtrl;
        if( previous != text.size() || previous_state != state ) {
            std::fprintf( stderr, "Brick GUI input: bytes=%zu active=%d ctrl=%d\\n",
                          text.size(), ImGui::IsItemActive(), ImGui::GetIO().KeyCtrl );
            previous = text.size();
            previous_state = state;
        }
    }'''
    assert text.count(anchor) == 1
    path.write_text(text.replace(anchor, anchor + insert, 1))

# UI block extracted from archived apply-port.py:802-802
path = source / 'src/sdltiles.cpp'

# UI block extracted from archived apply-port.py:803-803
text = path.read_text()

# UI block extracted from archived apply-port.py:804-816
if '// Brick: synthetic keyboard events belong to the game window.' not in text:
    anchor = '        imclient->process_input( &ev );'
    insert = '\n'.join([
        '        // Brick: synthetic keyboard events belong to the game window.',
        '        if( std::getenv( "CDDA_BRICK" ) ) {',
        '            const Uint32 id = SDL_GetWindowID( window.get() );',
        '            if( ev.type == SDL_TEXTINPUT && !ev.text.windowID ) { ev.text.windowID = id; }',
        '            if( ( ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP ) && !ev.key.windowID ) { ev.key.windowID = id; }',
        '            if( ev.type == SDL_WINDOWEVENT && !ev.window.windowID ) { ev.window.windowID = id; }',
        '        }',
    ])
    assert text.count(anchor) == 1
    path.write_text(text.replace(anchor, insert + '\n' + anchor, 1))

# UI block extracted from archived apply-port.py:819-819
path = source / 'src/cata_imgui.cpp'

# UI block extracted from archived apply-port.py:820-820
text = path.read_text()

# UI block extracted from archived apply-port.py:821-821
old = '    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls'

# UI block extracted from archived apply-port.py:822-824
if old in text:
    text = text.replace(old, '    if( !std::getenv( "CDDA_BRICK" ) ) {\n        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;\n    } // Brick joystick input is resolved by the contextual adapter.')
    path.write_text(text)

# UI block extracted from archived apply-port.py:827-827
path = source / 'src/cata_imgui.cpp'

# UI block extracted from archived apply-port.py:828-828
text = path.read_text()

# UI block extracted from archived apply-port.py:829-860
if 'namespace brick_gui' not in text:
    text = '#include "brick_gui.h"\n' + text
    anchor = '    cata_input_trail.clear();'
    assert text.count(anchor) == 2
    text = text.replace(anchor, anchor + '\n    brick_gui::release();')
    text = text.replace('    ImGui::NewFrame();', '    brick_gui::apply();\n    ImGui::NewFrame();')
    text += '''
namespace brick_gui
{
namespace { std::vector<int> keys; std::vector<int> pending; }
void press( int key )
{
    pending.push_back( key );
}
void apply()
{
    for( const int key : pending ) {
        ImGui::GetIO().AddKeyEvent( static_cast<ImGuiKey>( key ), true );
        keys.push_back( key );
    }
    pending.clear();
}
void release()
{
    for( const int key : keys ) {
        ImGui::GetIO().AddKeyEvent( static_cast<ImGuiKey>( key ), false );
    }
    keys.clear();
}
}
'''
    path.write_text(text)

# UI block extracted from archived apply-port.py:861-861
path = source / 'src/input_popup.cpp'

# UI block extracted from archived apply-port.py:862-862
text = path.read_text()

# UI block extracted from archived apply-port.py:863-880
if 'brick_gui::press' not in text:
    text = '#include "brick_gui.h"\n' + text
    text = text.replace('            io.AddKeyEvent( key, true );\n            io.AddKeyEvent( key, false );',
                        '            brick_gui::press( key );', 1)
    old = '''            io.AddKeyEvent( ImGuiMod_Ctrl, true );
            io.AddKeyEvent( ImGuiKey_A, true );
            io.AddKeyEvent( ImGuiKey_A, false );
            io.AddKeyEvent( ImGuiKey_Backspace, true );
            io.AddKeyEvent( ImGuiKey_Backspace, false );
            io.AddKeyEvent( ImGuiMod_Ctrl, false );'''
    new = '''            brick_gui::press( ImGuiMod_Ctrl );
            brick_gui::press( ImGuiKey_A );
            brick_gui::press( ImGuiKey_Backspace );'''
    assert text.count(old) == 1
    text = text.replace(old, new, 1)
    text = text.replace('        ImGuiIO &io = ImGui::GetIO();\n        const ImGuiKey key = action',
                        '        const ImGuiKey key = action', 1)
    path.write_text(text)

for helper in ('fit-brick-menus.py', 'fix-gui-fallback.py', 'controller-ui.py', 'sidebar-layout.py'):
    subprocess.run([sys.executable, str(port / helper), str(source)], check=True)
print('Applied UI-only controller, fonts, hints, menus and sidebar. Platform build still required.')
