#!/usr/bin/env python3
"""Keep I's paragraph-sized menus inside the Brick viewport."""
from pathlib import Path
import sys

p = Path(sys.argv[1]) / 'src/uilist.cpp'
t = p.read_text()
if '// Brick: wrap long menu paragraphs before sizing the window.' not in t:
    old = '''static ImVec2 calc_size( std::string_view line )
{
    return ImGui::CalcTextSize( remove_color_tags( line ).c_str() );
}'''
    new = '''// Brick: wrap long menu paragraphs before sizing the window.
static float brick_menu_wrap_width()
{
    if( !std::getenv( "CDDA_BRICK" ) ) { return 0.f; }
    const ImGuiStyle &style = ImGui::GetStyle();
    return std::max( 1.f, .9f * ImGui::GetMainViewport()->Size.x -
                     2.f * ( style.WindowPadding.x + style.WindowBorderSize ) );
}

static ImVec2 calc_size( std::string_view line, float wrap_width = 0.f )
{
    const std::string clean = remove_color_tags( line );
    if( wrap_width <= 0.f ) { return ImGui::CalcTextSize( clean.c_str() ); }
    const int cells = std::max( 1, int( wrap_width / ImGui::CalcTextSize( " " ).x ) );
    ImVec2 size( 0.f, 0.f );
    for( const std::string &part : foldstring( clean, cells ) ) {
        const ImVec2 row = ImGui::CalcTextSize( part.c_str() );
        size.x = std::max( size.x, row.x );
        size.y += row.y;
    }
    return size;
}'''
    assert t.count(old) == 1
    t = t.replace(old,new)
    # The draw implementation precedes the sizing helpers.
    anchor = 'class uilist_impl : cataimgui::window'
    t = t.replace(anchor, 'static float brick_menu_wrap_width();\n\n'+anchor,1)
    t = t.replace('cataimgui::draw_colored_text( parent.text );',
                  'cataimgui::draw_colored_text( parent.text, brick_menu_wrap_width() );')
    t = t.replace('cataimgui::draw_colored_text( description );',
                  'cataimgui::draw_colored_text( description, brick_menu_wrap_width() );')
    for old,new in [('text_size = calc_size( text );','text_size = calc_size( text, brick_menu_wrap_width() );'),
                    ('desc_size = calc_size( footer_text );','desc_size = calc_size( footer_text, brick_menu_wrap_width() );'),
                    ('ImVec2 entry_size = calc_size( ent.desc );','ImVec2 entry_size = calc_size( ent.desc, brick_menu_wrap_width() );')]:
        assert t.count(old)==1
        t=t.replace(old,new,1)
    anchor='''    calculated_bounds.h = calculated_menu_size.y + additional_height;'''
    insert='''
    if( std::getenv( "CDDA_BRICK" ) ) {
        calculated_bounds.w = std::min( calculated_bounds.w,
                                       .9f * ImGui::GetMainViewport()->Size.x );
        longest_line_width = std::min( longest_line_width,
                                      std::max( 1.f, brick_menu_wrap_width() -
                                                extra_space_left - extra_space_right ) );
        calculated_menu_size.x = std::min( calculated_menu_size.x, longest_line_width );
        calculated_label_width = std::max( 1.f, calculated_menu_size.x -
                                 calculated_hotkey_width - calculated_secondary_width - 3.f * padding );
        if( desired_bounds.has_value() && desired_bounds->w > calculated_bounds.w ) {
            desired_bounds->w = calculated_bounds.w;
        }
    }'''
    assert t.count(anchor)==1
    t=t.replace(anchor,anchor+insert,1)
    p.write_text(t)
print('Applied viewport-sized Brick menu paragraphs and columns.')
