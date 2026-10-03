#include "brick_input.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

static void require( bool okay, const char *message )
{
    if( !okay ) { std::fprintf( stderr, "FAIL: %s\n", message ); std::exit( 1 ); }
}
static void hat( Uint8 value )
{
    SDL_Event ev{}; ev.type = SDL_JOYHATMOTION; ev.jhat.value = value;
    require( SDL_PushEvent( &ev ) == 1, "push direction" );
}
static int delayed_button( void * )
{
    SDL_Delay( 10 );
    SDL_Event ev{}; ev.type = SDL_JOYBUTTONDOWN; ev.jbutton.button = 1;
    SDL_PushEvent( &ev ); return 0;
}
int main()
{
    setenv( "CDDA_BRICK", "1", 1 );
    require( SDL_Init( SDL_INIT_TIMER | SDL_INIT_EVENTS ) == 0, "SDL init" );
    brick_input::init(); brick_input::set_context( "DEFAULTMODE" ); SDL_Event ev{};
    while( brick_input::poll_event( &ev ) ) {}
    unsigned wakeups = 0;
    const Uint32 started = SDL_GetTicks();
    timespec before{}, after{};
    clock_gettime( CLOCK_THREAD_CPUTIME_ID, &before );
    while( SDL_GetTicks() - started < 200 ) {
        brick_input::wait_for_event( 25 ); ++wakeups;
        require( !brick_input::poll_event( &ev ), "idle has no input" );
    }
    clock_gettime( CLOCK_THREAD_CPUTIME_ID, &after );
    const double cpu_ms = ( after.tv_sec - before.tv_sec ) * 1000.0 +
                         ( after.tv_nsec - before.tv_nsec ) / 1000000.0;
    require( wakeups <= 16, "idle wait must not spin at 1 kHz" );
    SDL_Thread *thread = SDL_CreateThread( delayed_button, "input-inject", nullptr );
    require( thread != nullptr, "input thread" );
    const Uint32 key_started = SDL_GetTicks();
    brick_input::wait_for_event( 25 );
    require( brick_input::poll_event( &ev ) && ev.type == SDL_KEYDOWN &&
             ev.key.keysym.sym == SDLK_RETURN, "wait wakes on A and preserves event" );
    require( SDL_GetTicks() - key_started < 100, "prompt wakeup" );
    SDL_WaitThread( thread, nullptr );
    hat( SDL_HAT_UP ); hat( SDL_HAT_RIGHTUP );
    require( !brick_input::poll_event( &ev ), "diagonal waits for coalescing" );
    const Uint32 direction_started = SDL_GetTicks();
    while( !brick_input::poll_event( &ev ) ) {
        require( SDL_GetTicks() - direction_started < 100, "direction deadline" );
        brick_input::wait_for_event( 25 );
    }
    require( ev.type == SDL_TEXTINPUT && std::string( ev.text.text ) == "9", "diagonal survives waiting" );
    hat( SDL_HAT_CENTERED );
    while( brick_input::poll_event( &ev ) ) {}
    // A queued event must remain in the queue after WaitEventTimeout(nullptr).
    SDL_Event queued{}; queued.type = SDL_JOYBUTTONDOWN; queued.jbutton.button = 0;
    require( SDL_PushEvent( &queued ) == 1, "queue B" );
    brick_input::wait_for_event( 25 );
    require( brick_input::poll_event( &ev ) && ev.key.keysym.sym == SDLK_ESCAPE, "queued B survives wait" );
    // Capture the opening action before any expensive menu work; idle time is excluded.
    const char *trace = std::getenv( "CDDA_BRICK_TRACE_PATH" );
    require( trace != nullptr, "trace fixture path" );
    brick_input::set_context( "DEFAULTMODE" );
    brick_input::note_action( "inventory" );
    SDL_Delay( 1050 );
    brick_input::set_context( "INVENTORY" );
    brick_input::begin_input_wait();
    SDL_Delay( 1050 );
    brick_input::begin_input_wait();
    FILE *file = std::fopen( trace, "rb" ); require( file != nullptr, "trace output" );
    char buffer[4096]{}; std::fread( buffer, 1, sizeof( buffer ) - 1, file ); std::fclose( file );
    const std::string contents( buffer );
    require( contents.find( "ACTION_BEGIN" ) != std::string::npos, "opening breadcrumb" );
    const auto slow = contents.find( "SLOW_PROCESSING" );
    require( slow != std::string::npos && contents.find( "SLOW_PROCESSING", slow + 1 ) == std::string::npos,
             "processing is recorded once; idle reading does not become a slow action" );
    // A long session must rotate diagnostics instead of growing indefinitely.
    file = std::fopen( trace, "wb" ); require( file != nullptr, "trace rotation fixture" );
    for( int i = 0; i < 65536; ++i ) { std::fputc( 'x', file ); }
    std::fclose( file );
    brick_input::set_context( "DEFAULTMODE" );
    brick_input::note_action( "craft" );
    file = std::fopen( ( std::string( trace ) + ".previous" ).c_str(), "rb" );
    require( file != nullptr, "previous trace retained" );
    std::fseek( file, 0, SEEK_END ); require( std::ftell( file ) == 65536, "rotation preserves previous trace" );
    std::fclose( file );
    file = std::fopen( trace, "rb" ); require( file != nullptr, "current trace after rotation" );
    std::fseek( file, 0, SEEK_END ); require( std::ftell( file ) < 2048, "current trace remains bounded" );
    std::fclose( file );
    brick_input::shutdown(); SDL_Quit();
    std::printf( "PASS: idle 200ms=%u wakeups, thread CPU=%.3fms; event delivery, diagonal deadlines and bounded processing trace\n", wakeups, cpu_ms );
}
