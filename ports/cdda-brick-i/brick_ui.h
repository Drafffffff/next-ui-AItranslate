#ifndef CDDA_BRICK_UI_H
#define CDDA_BRICK_UI_H
#include <cstdlib>
#include <cstring>

namespace brick_ui
{
// Presentation only. Never remove keyboard bindings or item inventory letters.
inline bool controller_hints_only()
{
#if defined(TILES)
    const char *value = std::getenv( "CDDA_BRICK" );
    return value && std::strcmp( value, "1" ) == 0;
#else
    return false;
#endif
}
}
#endif
