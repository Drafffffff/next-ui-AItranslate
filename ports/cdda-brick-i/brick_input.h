#ifndef CDDA_BRICK_INPUT_H
#define CDDA_BRICK_INPUT_H

#include <SDL.h>
#include <string>
#include <vector>

namespace brick_input
{
void init();
int reserved_height();
void set_context( const std::string &context, const std::vector<std::string> &actions = {} );
void set_crafting_view( bool wide );
void set_overmap_cursor_mode( bool fast, int step );
// Private SDL events carry logical actions, never item hotkeys or typed text.
int event_action_code( const SDL_Event &event );
const std::string &mapped_action( int code );
std::string control_hint( bool second_line );
int text_cells( const std::string &text );
void shutdown();
void reset_after_suspend();
int poll_event( SDL_Event *event );
void wait_for_event( int maximum_ms );
void note_suspend( const std::string &phase );
void note_action( const std::string &action );
void begin_input_wait();
void render_overlay( SDL_Renderer *renderer );
}

#endif
