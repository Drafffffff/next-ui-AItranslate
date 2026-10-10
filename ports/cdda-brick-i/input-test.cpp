#include "brick_input.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
void require( bool value, const char *message )
{
    if( !value ) {
        std::fprintf( stderr, "FAIL: %s\n", message );
        std::exit( 1 );
    }
}
SDL_Event next( bool refresh = false );
void push_button( int button, bool down = true )
{
    SDL_Event event{};
    event.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
    event.jbutton.button = static_cast<Uint8>( button );
    require( SDL_PushEvent( &event ) == 1, "queue button" );
    if( button == 8 && down ) {
        require( next( true ).type == SDL_WINDOWEVENT, "modifier refresh" );
    }
}
void push_hat( Uint8 value )
{
    SDL_Event event{};
    event.type = SDL_JOYHATMOTION;
    event.jhat.value = value;
    require( SDL_PushEvent( &event ) == 1, "queue direction" );
}
SDL_Event next( bool refresh )
{
    SDL_Event event{};
    // SDL 2.26+ may end one pump cycle at its sentinel before a newly queued event.
    for( int i = 0; i < 100; ++i ) {
        if( brick_input::poll_event( &event ) == 1 &&
            ( refresh || event.type != SDL_WINDOWEVENT || event.window.event != SDL_WINDOWEVENT_EXPOSED ) ) {
            return event;
        }
        SDL_Delay( 1 );
    }
    require( false, "expected event within 100 ms" );
    return event;
}
bool no_event()
{
    SDL_Event event{};
    while( brick_input::poll_event( &event ) ) {
        if( event.type != SDL_WINDOWEVENT || event.window.event != SDL_WINDOWEVENT_EXPOSED ) {
            return false;
        }
    }
    return true;
}
void char_is( const SDL_Event &event, const char *text )
{
    require( event.type == SDL_TEXTINPUT && std::strcmp( event.text.text, text ) == 0,
             text );
}
}

int main()
{
    setenv( "CDDA_BRICK", "1", 1 );
    require( SDL_Init( SDL_INIT_TIMER | SDL_INIT_EVENTS ) == 0, "SDL init" );
    brick_input::init();
    brick_input::set_context( "DEFAULTMODE" );
    while( !no_event() ) {}
    push_button( 1 );
    require( next().key.keysym.sym == SDLK_RETURN, "A confirms" );
    push_button( 0 );
    require( next().key.keysym.sym == SDLK_ESCAPE, "B returns" );
    push_button( 3 ); char_is( next(), "g" );
    push_button( 2 ); char_is( next(), "5" );
    push_button( 9 ); char_is( next(), ";" );
    push_button( 10 ); char_is( next(), "m" );

    // Rolling a diagonal must not first move north, and holding repeats once due.
    push_hat( SDL_HAT_UP );
    push_hat( SDL_HAT_RIGHTUP );
    require( no_event(), "coalesce diagonal" );
    SDL_Delay( 45 ); char_is( next(), "9" );
    require( no_event(), "no premature repeat" );
    SDL_Delay( 360 ); char_is( next(), "9" );
    push_hat( SDL_HAT_UP );
    push_hat( SDL_HAT_CENTERED );
    require( no_event(), "release stops repeat" );

    // Even a very short diagonal is emitted once, with no step on partial release.
    push_hat( SDL_HAT_UP ); push_hat( SDL_HAT_RIGHTUP );
    push_hat( SDL_HAT_UP ); push_hat( SDL_HAT_CENTERED );
    char_is( next(), "9" );
    require( no_event(), "short diagonal emits once" );

    // A short tap still emits once even before the diagonal timer expires.
    push_hat( SDL_HAT_LEFT );
    push_hat( SDL_HAT_CENTERED );
    require( next().key.keysym.sym == SDLK_LEFT, "short directional tap" );
    require( no_event(), "short tap emits once" );

    push_button( 8 ); push_button( 7 ); char_is( next(), "S" );
    push_button( 8, false ); require( no_event(), "save chord does not open help" );
    push_button( 8 ); push_button( 1 ); char_is( next(), "Y" );
    push_button( 8, false ); require( no_event(), "yes chord release" );
    push_button( 8 ); push_hat( SDL_HAT_RIGHT );
    require( no_event(), "stairs direction delay" );
    push_button( 8, false ); char_is( next(), ">" );
    require( no_event(), "modifier release stops directions" );

    push_button( 8 ); push_button( 6 );
    require( next( true ).type == SDL_WINDOWEVENT, "open keyboard" );
    push_button( 8, false ); require( no_event(), "keyboard stays open" );
    push_button( 1 ); char_is( next(), "1" );
    push_button( 3 ); require( next().key.keysym.sym == SDLK_BACKSPACE, "keyboard delete" );
    push_hat( SDL_HAT_DOWN ); push_hat( SDL_HAT_CENTERED ); next( true );
    push_button( 2 ); next( true ); // uppercase
    push_button( 1 ); char_is( next(), "Q" );
    // Symbol page gives access to bindings such as disassemble and haul.
    for( int i = 0; i < 4; ++i ) {
        push_hat( SDL_HAT_DOWN ); push_hat( SDL_HAT_CENTERED ); next( true );
    }
    push_hat( SDL_HAT_LEFT ); push_hat( SDL_HAT_CENTERED ); next( true ); // symbols, index 59
    push_button( 1 ); require( next( true ).type == SDL_WINDOWEVENT, "next symbol page" );
    push_hat( SDL_HAT_UP ); push_hat( SDL_HAT_CENTERED ); next( true );
    push_button( 1 ); char_is( next(), "~" );
    for( int i = 0; i < 4; ++i ) {
        push_hat( SDL_HAT_LEFT ); push_hat( SDL_HAT_CENTERED ); next( true );
    }
    push_button( 1 ); char_is( next(), "\\" );
    push_button( 7 ); require( next().key.keysym.sym == SDLK_RETURN, "keyboard accepts" );
    push_button( 3 ); char_is( next(), "g" ); // normal controls restored

    push_button( 8 ); push_button( 8, false ); next( true ); // action panel
    push_hat( SDL_HAT_UP ); push_hat( SDL_HAT_CENTERED ); next( true );
    push_button( 1 ); char_is( next(), "\"" );
    push_button( 8 ); push_button( 8, false ); next( true );
    push_button( 5 ); next( true ); // items page
    push_hat( SDL_HAT_LEFTUP ); push_hat( SDL_HAT_CENTERED ); next( true );
    push_button( 1 ); char_is( next(), "&" );
    push_button( 8 ); push_button( 8, false ); next( true );
    push_button( 3 ); next( true ); // help
    push_button( 3 ); require( next( true ).type == SDL_WINDOWEVENT, "help absorbs gameplay input" );
    SDL_Event physical{};
    physical.type = SDL_KEYDOWN;
    physical.key.keysym.sym = SDLK_q;
    SDL_PushEvent( &physical );
    require( next( true ).type == SDL_WINDOWEVENT, "help absorbs physical key" );
    physical = SDL_Event{};
    physical.type = SDL_TEXTINPUT;
    std::strcpy( physical.text.text, "q" );
    SDL_PushEvent( &physical );
    require( next( true ).type == SDL_WINDOWEVENT, "help absorbs companion text event" );
    push_button( 0 ); next( true );
    push_button( 3 ); char_is( next(), "g" );

    // Focus changes must not leave the modifier or movement held.
    push_button( 8 ); require( no_event(), "hold modifier" );
    SDL_Event lost{};
    lost.type = SDL_WINDOWEVENT;
    lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    SDL_PushEvent( &lost ); next( true );
    push_button( 7 ); require( next().key.keysym.sym == SDLK_TAB, "modifier cleared on focus loss" );
    brick_input::shutdown();

    // Exercise SDL's real joystick event path and instance IDs with a virtual device.
    require( SDL_InitSubSystem( SDL_INIT_JOYSTICK ) == 0, "virtual joystick subsystem" );
    const int device = SDL_JoystickAttachVirtual( SDL_JOYSTICK_TYPE_GAMECONTROLLER, 0, 15, 1 );
    require( device >= 0, "attach virtual joystick" );
    brick_input::init();
    brick_input::set_context( "DEFAULTMODE" );
    while( !no_event() ) {}
    SDL_Joystick *joystick = SDL_JoystickOpen( device );
    require( joystick != nullptr, "open virtual joystick" );
    require( SDL_JoystickSetVirtualButton( joystick, 1, 1 ) == 0, "virtual A down" );
    SDL_JoystickUpdate();
    require( next().key.keysym.sym == SDLK_RETURN, "driver event reaches adapter" );
    SDL_JoystickSetVirtualButton( joystick, 1, 0 );
    SDL_JoystickSetVirtualButton( joystick, 8, 1 );
    SDL_JoystickUpdate();
    while( !no_event() ) {}
    SDL_JoystickClose( joystick );
    require( SDL_JoystickDetachVirtual( device ) == 0, "detach virtual joystick" );
    while( !no_event() ) {}
    push_button( 7 ); require( next().key.keysym.sym == SDLK_TAB, "unplug clears held modifier" );
    brick_input::shutdown();
    SDL_Quit();
    std::puts( "PASS: native buttons, diagonal coalescing, repeat/release, save/yes chords, keyboard, focus cleanup, SDL virtual device/unplug" );
}
