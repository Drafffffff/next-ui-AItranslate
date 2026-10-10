#include "brick_input.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static void require( bool value, const char *message )
{
    if( !value ) { std::fprintf( stderr, "FAIL: %s\n", message ); std::exit( 1 ); }
}
static void push( unsigned button, bool down = true )
{
    SDL_Event ev{}; ev.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
    ev.jbutton.button = button; require( SDL_PushEvent( &ev ) == 1, "queue button" );
}
static void hat( Uint8 value )
{
    SDL_Event ev{}; ev.type = SDL_JOYHATMOTION; ev.jhat.value = value;
    require( SDL_PushEvent( &ev ) == 1, "queue hat" );
}
static void axis( unsigned index, Sint16 value )
{
    SDL_Event ev{}; ev.type = SDL_JOYAXISMOTION; ev.jaxis.axis = index;
    ev.jaxis.value = value; require( SDL_PushEvent( &ev ) == 1, "queue trigger axis" );
}
static SDL_Event next()
{
    SDL_Event ev{};
    for( int i = 0; i < 100; ++i ) {
        if( brick_input::poll_event( &ev ) && ev.type != SDL_WINDOWEVENT ) { return ev; }
        SDL_Delay( 1 );
    }
    require( false, "expected command" ); return ev;
}
static bool empty()
{
    SDL_Event ev{};
    while( brick_input::poll_event( &ev ) ) { if( ev.type != SDL_WINDOWEVENT ) { return false; } }
    return true;
}
static void drain() { require( empty(), "no unintended input" ); }
static void expect( const char *id )
{
    const SDL_Event ev = next();
    const int code = brick_input::event_action_code( ev );
    require( code > 0 && brick_input::mapped_action( code ) == id, id );
    require( ev.type != SDL_TEXTINPUT && ev.type != SDL_KEYDOWN, "command cannot be an inventory letter" );
}
static void hints_fit()
{
    for( bool second : {false, true} ) {
        const auto text = brick_input::control_hint( second );
        if( brick_input::text_cells( text ) > 84 ) { std::fprintf( stderr, "Too wide: %s\n", text.c_str() ); }
        require( brick_input::text_cells( text ) <= 84, "hint fits 1020 px" );
    }
}
int main()
{
    setenv( "CDDA_BRICK", "1", 1 );
    require( SDL_Init( SDL_INIT_TIMER | SDL_INIT_EVENTS ) == 0, "SDL init" );
    brick_input::init(); drain();
    std::vector<std::string> inv{"UP","DOWN","NEXT_COLUMN","PREV_COLUMN","CONFIRM","QUIT",
        "PAGE_UP","PAGE_DOWN","INVENTORY_FILTER","RESET_FILTER","TOGGLE_FAVORITE",
        "CATEGORY_SELECTION","EXAMINE","SHOW_HIDE_CONTENTS","HOME","END"};
    brick_input::set_context( "INVENTORY", inv ); drain(); hints_fit();
    for( const auto &pair : std::vector<std::pair<unsigned,const char *>>{
        {0,"QUIT"},{1,"CONFIRM"},{3,"INVENTORY_FILTER"},{2,"TOGGLE_FAVORITE"},
        {4,"PREV_COLUMN"},{5,"NEXT_COLUMN"},{6,"CATEGORY_SELECTION"},
        {11,"PAGE_UP"},{12,"PAGE_DOWN"},
        {9,"EXAMINE"},{10,"SHOW_HIDE_CONTENTS"}} ) { push( pair.first ); expect( pair.second ); }
    // Actual firmware trigger axes: one action per press, with noise hysteresis.
    axis( 2, -32768 ); drain(); axis( 2, 10000 ); drain();
    axis( 2, 20000 ); expect( "PAGE_UP" );
    axis( 2, 32767 ); drain(); axis( 2, 14000 ); drain();
    axis( 2, 20000 ); drain(); axis( 2, 0 ); drain();
    axis( 2, 32767 ); expect( "PAGE_UP" ); axis( 2, -32768 ); drain();
    axis( 5, 32767 ); expect( "PAGE_DOWN" ); axis( 5, -32768 ); drain();
    axis( 0, 32767 ); drain();
    hat( SDL_HAT_RIGHTUP ); hat( SDL_HAT_CENTERED ); expect( "UP" );
    hat( SDL_HAT_LEFT ); hat( SDL_HAT_CENTERED ); expect( "PREV_COLUMN" );
    // Inventory picking uses a distinct registered action, not a mouse SELECT event.
    inv.insert( inv.end(), {"TOGGLE_ENTRY","INCREASE_COUNT","DECREASE_COUNT","MARK_WITH_COUNT"} );
    brick_input::set_context( "INVENTORY", inv ); drain(); hints_fit();
    push( 2 ); expect( "TOGGLE_ENTRY" );
    push( 8 ); drain(); hints_fit(); push( 9 ); expect( "DECREASE_COUNT" );
    push( 10 ); expect( "INCREASE_COUNT" ); push( 3 ); expect( "RESET_FILTER" );
    push( 8, false ); drain();
    // Context panel executes semantic filter action; never '&' (game crafting).
    push( 8 ); push( 8, false ); drain(); hints_fit();
    hat( SDL_HAT_UP ); hat( SDL_HAT_CENTERED ); drain(); push( 1 ); expect( "INVENTORY_FILTER" );
    brick_input::set_context( "CRAFTING", {"UP","DOWN","LEFT","RIGHT","CONFIRM","QUIT",
        "PREV_TAB","NEXT_TAB","FILTER","RESET_FILTER","TOGGLE_FAVORITE","CYCLE_BATCH",
        "HELP_RECIPE","RELATED_RECIPES","PAGE_UP","PAGE_DOWN","HOME","END",
        "SCROLL_RECIPE_INFO_UP","SCROLL_RECIPE_INFO_DOWN"} );
    drain(); hints_fit();
    push( 4 ); expect( "PREV_TAB" ); push( 5 ); expect( "NEXT_TAB" );
    push( 11 ); expect( "SCROLL_RECIPE_INFO_UP" ); push( 12 ); expect( "SCROLL_RECIPE_INFO_DOWN" );
    push( 7 ); drain(); push( 0 ); drain(); // START panel closes without changing a tab.
    push( 2 ); expect( "CYCLE_BATCH" ); push( 6 ); expect( "TOGGLE_FAVORITE" );
    push( 9 ); expect( "HELP_RECIPE" ); push( 10 ); expect( "RELATED_RECIPES" );
    push( 3 ); expect( "FILTER" );
    brick_input::set_context( "ADVANCED_INVENTORY", {"UP","DOWN","LEFT","RIGHT","QUIT",
        "EXAMINE","FILTER","MOVE_ITEM_STACK","TOGGLE_TAB","ITEMS_INVENTORY","PAGE_UP","PAGE_DOWN"} );
    drain(); hints_fit();
    push( 4 ); expect( "LEFT" ); push( 4 ); expect( "LEFT" ); // Never toggle back to right.
    push( 5 ); expect( "RIGHT" ); push( 5 ); expect( "RIGHT" );
    push( 11 ); expect( "PAGE_UP" ); push( 12 ); expect( "PAGE_DOWN" );
    push( 6 ); expect( "ITEMS_INVENTORY" );
    push( 7 ); drain(); push( 0 ); drain();
    brick_input::set_context( "STRING_INPUT", {"TEXT.CONFIRM","TEXT.QUIT","TEXT.LEFT",
        "TEXT.RIGHT","TEXT.BACKSPACE","TEXT.CLEAR","ANY_INPUT"} ); drain(); hints_fit();
    push( 1 ); require( std::string( next().text.text ) == "1", "auto keyboard types" );
    push( 4 ); expect( "TEXT.LEFT" ); push( 5 ); expect( "TEXT.RIGHT" );
    push( 3 ); expect( "TEXT.BACKSPACE" ); push( 6 ); require( std::string( next().text.text ) == " ", "space shortcut" );
    push( 9 ); expect( "TEXT.CLEAR" );
    push( 7 ); expect( "TEXT.CONFIRM" );
    brick_input::set_context( "CRAFTING", {"UP","DOWN","CONFIRM","QUIT"} ); drain();
    push( 3 ); drain(); // Unavailable filter does not type a gameplay shortcut.
    brick_input::set_context( "STRING_INPUT", {"TEXT.CONFIRM","TEXT.QUIT"} ); drain();
    push( 0 ); expect( "TEXT.QUIT" );
    brick_input::set_context( "OVERMAP", {"UP","DOWN","LEFT","RIGHT","RIGHTUP","CONFIRM","QUIT",
        "SEARCH","CENTER","zoom_in","zoom_out","LEVEL_UP","LEVEL_DOWN","LIST_NOTES","TOGGLE_FAST_SCROLL"} );
    drain(); hints_fit();
    push( 3 ); expect( "SEARCH" ); push( 2 ); expect( "CENTER" );
    push( 4 ); expect( "zoom_out" ); push( 5 ); expect( "zoom_in" );
    push( 9 ); expect( "LEVEL_UP" ); push( 10 ); expect( "LEVEL_DOWN" );
    hat( SDL_HAT_RIGHTUP ); hat( SDL_HAT_CENTERED ); expect( "RIGHTUP" );
    brick_input::set_context( "YESNO", {"YES","NO","CONFIRM","LEFT","RIGHT"} ); drain(); hints_fit();
    push( 1 ); expect( "YES" ); push( 0 ); expect( "NO" );
    // Fast menu repeat starts sooner than movement; exiting cancels a held direction.
    brick_input::set_context( "INVENTORY", inv ); drain();
    hat( SDL_HAT_DOWN ); expect( "DOWN" ); SDL_Delay( 285 ); expect( "DOWN" );
    SDL_Delay( 70 ); expect( "DOWN" );
    brick_input::set_context( "DEFAULTMODE" ); drain(); SDL_Delay( 120 ); drain();
    hat( SDL_HAT_CENTERED ); drain();
    push( 3 ); require( std::string( next().text.text ) == "g", "gameplay restored" );
    SDL_Event unrelated{}; unrelated.type = SDL_USEREVENT; unrelated.user.code = 123;
    require( brick_input::event_action_code( unrelated ) == 0, "foreign user event ignored" );
    brick_input::set_context( "UILIST", {"UILIST.UP","UILIST.DOWN","UILIST.LEFT","UILIST.RIGHT",
        "UILIST.QUIT","UILIST.FILTER","CONFIRM","PAGE_UP","PAGE_DOWN"} ); drain(); hints_fit();
    push( 0 ); expect( "UILIST.QUIT" ); push( 3 ); expect( "UILIST.FILTER" );
    hat( SDL_HAT_DOWN ); hat( SDL_HAT_CENTERED ); expect( "UILIST.DOWN" );
    hat( SDL_HAT_LEFT ); hat( SDL_HAT_CENTERED ); expect( "UILIST.LEFT" );
    brick_input::shutdown(); SDL_Quit();
    std::puts( "PASS: contextual inventory/multiselect/crafting/map/yes-no actions, automatic text keyboard, hint widths, fast menu repeat and held-input boundary" );
}
