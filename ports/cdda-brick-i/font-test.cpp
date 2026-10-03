#include "brick_input.h"
#include <SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
int main()
{
    if( TTF_Init() != 0 ) { return 1; }
    TTF_Font *ascii = TTF_OpenFont( "data/font/Terminus.ttf", 30 );
    TTF_Font *chinese = TTF_OpenFont( "data/font/unifont.ttf", 30 );
    if( !ascii || !chinese ) { std::fprintf( stderr, "%s\n", TTF_GetError() ); return 1; }
    int ascii_advance = 0, chinese_advance = 0;
    for( Uint16 c : { Uint16('i'), Uint16('W'), Uint16('0'), Uint16('?') } ) {
        int advance = 0;
        if( TTF_GlyphMetrics( ascii, c, nullptr, nullptr, nullptr, nullptr, &advance ) != 0 ) { return 1; }
        if( ascii_advance && advance != ascii_advance ) { return 1; }
        ascii_advance = advance;
    }
    for( Uint16 c : { Uint16(0x4e2d), Uint16(0x6587), Uint16(0x5236), Uint16(0x4f5c) } ) {
        int advance = 0;
        if( TTF_GlyphMetrics( chinese, c, nullptr, nullptr, nullptr, nullptr, &advance ) != 0 ) { return 1; }
        if( chinese_advance && advance != chinese_advance ) { return 1; }
        chinese_advance = advance;
    }
    if( brick_input::text_cells("iii") != brick_input::text_cells("WWW") ||
        brick_input::text_cells("中文") != 4 || brick_input::text_cells("MENU中文12") != 10 ) { return 1; }
    std::printf("PASS: equal ASCII advances=%d, equal Chinese advances=%d, mixed one/two-cell layout\n",ascii_advance,chinese_advance);
    TTF_CloseFont(ascii); TTF_CloseFont(chinese); TTF_Quit();
    return 0;
}
