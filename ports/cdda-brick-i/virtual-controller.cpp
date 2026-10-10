// Validation only: control an SDL virtual Brick joystick inside the actual game.
// This shared library is never included in the installable package.
#include <SDL.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
SDL_Joystick *joystick = nullptr;
int command_fd = -1;
std::string pending;
std::string capture;
bool physical_input = false;
bool music_reported = false;

void receive_commands()
{
    if( command_fd < 0 || !joystick ) {
        return;
    }
    char buffer[512];
    ssize_t count;
    while( ( count = read( command_fd, buffer, sizeof( buffer ) ) ) > 0 ) {
        pending.append( buffer, static_cast<size_t>( count ) );
    }
    size_t newline;
    while( ( newline = pending.find( '\n' ) ) != std::string::npos ) {
        const std::string line = pending.substr( 0, newline );
        pending.erase( 0, newline + 1 );
        int index = 0, value = 0;
        if( std::sscanf( line.c_str(), "B %d %d", &index, &value ) == 2 ) {
            if( physical_input ) {
                SDL_Event event{};
                event.type = value ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
                event.jbutton.which = SDL_JoystickInstanceID( joystick );
                event.jbutton.button = static_cast<Uint8>( index );
                event.jbutton.state = value ? SDL_PRESSED : SDL_RELEASED;
                SDL_PushEvent( &event );
            } else {
                SDL_JoystickSetVirtualButton( joystick, index, value != 0 );
            }
        } else if( std::sscanf( line.c_str(), "AX %d %d", &index, &value ) == 2 ) {
            SDL_JoystickSetVirtualAxis( joystick, index, static_cast<Sint16>( value ) );
        } else if( std::sscanf( line.c_str(), "H %d", &value ) == 1 ) {
            if( physical_input ) {
                SDL_Event event{};
                event.type = SDL_JOYHATMOTION;
                event.jhat.which = SDL_JoystickInstanceID( joystick );
                event.jhat.value = static_cast<Uint8>( value );
                SDL_PushEvent( &event );
            } else {
                SDL_JoystickSetVirtualHat( joystick, 0, static_cast<Uint8>( value ) );
            }
        } else if( line.compare( 0, 2, "T " ) == 0 ) {
            SDL_Event event{}; event.type = SDL_TEXTINPUT;
            std::snprintf( event.text.text, sizeof( event.text.text ), "%s", line.c_str() + 2 );
            SDL_PushEvent( &event );
        } else if( std::sscanf( line.c_str(), "K %d", &value ) == 1 ) {
            SDL_Event event{}; event.type = SDL_KEYDOWN;
            event.key.keysym.sym = value;
            event.key.keysym.scancode = SDL_GetScancodeFromKey( value );
            SDL_PushEvent( &event );
        } else if( line.compare( 0, 2, "C " ) == 0 ) {
            const char *directory = std::getenv( "CDDA_TEST_CAPTURE" );
            if( directory ) {
                capture = std::string( directory ) + "/" + line.substr( 2 ) + ".bmp";
                SDL_Event refresh{};
                refresh.type = SDL_WINDOWEVENT;
                refresh.window.event = SDL_WINDOWEVENT_EXPOSED;
                SDL_PushEvent( &refresh );
            }
        }
        SDL_JoystickUpdate();
    }
}
}

extern "C" int SDL_Init( Uint32 flags )
{
    using init_t = int ( * )( Uint32 );
    static const auto original = reinterpret_cast<init_t>( dlsym( RTLD_NEXT, "SDL_Init" ) );
    const int result = original( flags );
    const char *fifo = std::getenv( "CDDA_TEST_FIFO" );
    if( result == 0 && fifo && command_fd < 0 ) {
        SDL_InitSubSystem( SDL_INIT_JOYSTICK );
        physical_input = std::getenv( "CDDA_TEST_PHYSICAL_INPUT" ) != nullptr;
        const int device = physical_input ? 0 :
                           SDL_JoystickAttachVirtual( SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 1 );
        if( device >= 0 ) { joystick = SDL_JoystickOpen( device ); }
        command_fd = open( fifo, O_RDONLY | O_NONBLOCK | O_CLOEXEC );
        if( !joystick || command_fd < 0 ) {
            std::fprintf( stderr, "Cannot initialize validation joystick/FIFO\n" );
            std::exit( 2 );
        }
        std::fprintf( stderr, "Validation joystick connected; physical=%d\n", physical_input );
    }
    return result;
}

extern "C" int SDL_PollEvent( SDL_Event *event )
{
    using poll_t = int ( * )( SDL_Event * );
    static const auto original = reinterpret_cast<poll_t>( dlsym( RTLD_NEXT, "SDL_PollEvent" ) );
    receive_commands();
    if( !music_reported ) {
        using playing_t = int (*)();
        const auto playing = reinterpret_cast<playing_t>( dlsym( RTLD_DEFAULT, "Mix_PlayingMusic" ) );
        if( playing && playing() ) {
            std::fprintf( stderr, "Validation: actual game music is playing\n" );
            music_reported = true;
        }
    }
    return original( event );
}

extern "C" void SDL_RenderPresent( SDL_Renderer *renderer )
{
    using present_t = void ( * )( SDL_Renderer * );
    static const auto original = reinterpret_cast<present_t>( dlsym( RTLD_NEXT, "SDL_RenderPresent" ) );
    if( !capture.empty() ) {
        int width = 0, height = 0;
        SDL_GetRendererOutputSize( renderer, &width, &height );
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat( 0, width, height, 32,
                               SDL_PIXELFORMAT_ARGB8888 );
        if( !surface || SDL_RenderReadPixels( renderer, nullptr, surface->format->format,
                                            surface->pixels, surface->pitch ) != 0 ||
            SDL_SaveBMP( surface, capture.c_str() ) != 0 ) {
            std::fprintf( stderr, "Validation capture failed: %s\n", SDL_GetError() );
        } else {
            std::fprintf( stderr, "Validation capture: %s\n", capture.c_str() );
        }
        SDL_FreeSurface( surface );
        capture.clear();
    }
    original( renderer );
}
