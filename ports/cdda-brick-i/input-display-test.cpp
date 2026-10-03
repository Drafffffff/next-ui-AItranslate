#include "brick_input.h"
#include <SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
SDL_Joystick *joystick;
void pump()
{
    const Uint32 end = SDL_GetTicks() + 50;
    do {
        SDL_Event event;
        while( brick_input::poll_event( &event ) ) {}
        SDL_Delay( 1 );
    } while( static_cast<Sint32>( SDL_GetTicks() - end ) < 0 );
}
void press( unsigned button, bool down )
{
    SDL_JoystickSetVirtualButton( joystick, static_cast<int>( button ), down ? 1 : 0 );
    SDL_JoystickUpdate();
    pump();
}
void snapshot( SDL_Renderer *renderer, const std::string &path )
{
    SDL_SetRenderDrawColor( renderer, 24, 27, 31, 255 );
    SDL_RenderClear( renderer );
    brick_input::render_overlay( renderer );
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat( 0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888 );
    if( !surface || SDL_RenderReadPixels( renderer, nullptr, surface->format->format,
                                        surface->pixels, surface->pitch ) != 0 ) {
        std::fprintf( stderr, "Snapshot failed: %s\n", SDL_GetError() );
        std::exit( 1 );
    }
    // A headless joystick without background events can silently draw nothing.
    const Uint32 background = SDL_MapRGBA( surface->format, 24, 27, 31, 255 );
    size_t changed = 0;
    for( int y = 0; y < surface->h; ++y ) {
        const auto *row = reinterpret_cast<const Uint32 *>(
                              static_cast<const Uint8 *>( surface->pixels ) + y * surface->pitch );
        for( int x = 0; x < surface->w; ++x ) {
            changed += row[x] != background;
        }
    }
    if( changed < 1000 || SDL_SaveBMP( surface, path.c_str() ) != 0 ) {
        std::fprintf( stderr, "Overlay absent or snapshot failed: %s\n", path.c_str() );
        std::exit( 1 );
    }
    SDL_FreeSurface( surface );
    SDL_RenderPresent( renderer );
}
}

int main( int argc, char **argv )
{
    if( argc != 2 ) {
        return 2;
    }
    setenv( "CDDA_BRICK", "1", 1 );
    if( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER ) != 0 ) {
        std::fprintf( stderr, "SDL display init: %s\n", SDL_GetError() );
        return 1;
    }
    if( TTF_Init() != 0 ) {
        std::fprintf( stderr, "TTF init: %s\n", TTF_GetError() );
        return 1;
    }
    const int device = SDL_JoystickAttachVirtual( SDL_JOYSTICK_TYPE_GAMECONTROLLER, 0, 15, 1 );
    if( device < 0 ) {
        std::fprintf( stderr, "Virtual joystick: %s\n", SDL_GetError() );
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow( "Brick input display test", 0, 0, 1024, 768, 0 );
    SDL_Renderer *renderer = window ? SDL_CreateRenderer( window, -1, SDL_RENDERER_SOFTWARE ) : nullptr;
    if( !renderer ) {
        std::fprintf( stderr, "Software renderer/window: %s\n", SDL_GetError() );
        return 1;
    }
    brick_input::init();
    joystick = SDL_JoystickOpen( device );
    pump();
    brick_input::set_context( "DEFAULTMODE" );
    pump();
    snapshot( renderer, std::string( argv[1] ) + "/hud.bmp" );
    press( 8, true );
    snapshot( renderer, std::string( argv[1] ) + "/hud-modifier.bmp" );
    press( 8, false );
    snapshot( renderer, std::string( argv[1] ) + "/actions.bmp" );
    press( 3, true ); press( 3, false );
    snapshot( renderer, std::string( argv[1] ) + "/help.bmp" );
    press( 0, true ); press( 0, false );
    press( 8, true ); press( 6, true ); press( 6, false ); press( 8, false );
    press( 1, true ); press( 1, false );
    SDL_JoystickSetVirtualHat( joystick, 0, SDL_HAT_DOWN ); SDL_JoystickUpdate(); pump();
    SDL_JoystickSetVirtualHat( joystick, 0, SDL_HAT_CENTERED ); SDL_JoystickUpdate(); pump();
    press( 2, true ); press( 2, false );
    press( 1, true ); press( 1, false );
    snapshot( renderer, std::string( argv[1] ) + "/keyboard.bmp" );
    brick_input::shutdown();
    SDL_JoystickClose( joystick );
    SDL_JoystickDetachVirtual( device );
    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    TTF_Quit();
    SDL_Quit();
    std::puts( "PASS: software renderer snapshots using the native SDL virtual joystick path" );
}
