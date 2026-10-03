#include "catacharset.h"
#include <clocale>
#include <cstdio>

int main()
{
    std::setlocale( LC_ALL, "C" );
    const std::string examples[] = { "", "Brick 123", "保存并退出？", "中文角色名", "A😀中" };
    for( const auto &example : examples ) {
        if( wstr_to_utf8( utf8_to_wstr( example ) ) != example ) {
            std::fprintf( stderr, "Failed UTF-8 round trip: %s\n", example.c_str() );
            return 1;
        }
    }
    if( utf8_to_wstr( "保存？" ).size() != 3 || utf8_to_wstr( "😀" ).size() != 1 ) {
        return 1;
    }
    std::puts( "PASS: C-locale UTF-8 round trips for ASCII, Chinese and non-BMP Unicode" );
}
