#if defined(TILES)
#include "brick_input.h"
#include <SDL_ttf.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace brick_input
{
namespace
{
constexpr unsigned menu_button = 8;
constexpr Uint32 diagonal_delay = 35;
constexpr Uint32 repeat_delay = 350;
constexpr Uint32 repeat_interval = 110;
constexpr Uint32 menu_repeat_delay = 280;
constexpr Uint32 menu_repeat_interval = 65;
constexpr int action_code_base = 0x110000;
struct command_t { std::string action; const char *label; };
// The IDs are private to this process; their order is not a saved binding.
const command_t commands[] = {
    {"UP", "上"}, {"DOWN", "下"}, {"LEFT", "左"}, {"RIGHT", "右"},
    {"LEFTUP", "左上"}, {"LEFTDOWN", "左下"}, {"RIGHTUP", "右上"}, {"RIGHTDOWN", "右下"},
    {"UILIST.UP", "上"}, {"UILIST.DOWN", "下"}, {"UILIST.LEFT", "左"},
    {"UILIST.RIGHT", "右"}, {"UILIST.QUIT", "返回"}, {"UILIST.FILTER", "搜索"},
    {"CONFIRM", "确认"}, {"QUIT", "返回"}, {"PAGE_UP", "上页"}, {"PAGE_DOWN", "下页"},
    {"PREV_COLUMN", "前栏"}, {"NEXT_COLUMN", "后栏"}, {"PREV_TAB", "前分类"}, {"NEXT_TAB", "后分类"},
    {"INVENTORY_FILTER", "搜索"}, {"FILTER", "搜索"}, {"SEARCH", "搜索"}, {"RESET_FILTER", "清除筛选"},
    {"TOGGLE_FAVORITE", "收藏"}, {"TOGGLE_ENTRY", "勾选"}, {"CATEGORY_SELECTION", "分类选择"},
    {"EXAMINE", "详情"}, {"EXAMINE_CONTENTS", "容器详情"}, {"SHOW_HIDE_CONTENTS", "展开收起"},
    {"SHOW_HIDE_CONTENTS_ALL", "展开所有"}, {"VIEW_CATEGORY_MODE", "分类视图"},
    {"HOME", "首项"}, {"END", "末项"}, {"MARK_WITH_COUNT", "指定数量"},
    {"INCREASE_COUNT", "数量加一"}, {"DECREASE_COUNT", "数量减一"},
    {"CYCLE_BATCH", "批量制作"}, {"HELP_RECIPE", "配方详情"}, {"RELATED_RECIPES", "相关配方"},
    {"SCROLL_RECIPE_INFO_UP", "右侧上滚"}, {"SCROLL_RECIPE_INFO_DOWN", "右侧下滚"},
    {"SCROLL_ITEM_INFO_UP", "详情上页"}, {"SCROLL_ITEM_INFO_DOWN", "详情下页"},
    {"TOGGLE_TAB", "切换左右栏"}, {"SORT", "排序"}, {"MOVE_SINGLE_ITEM", "移动单件"},
    {"MOVE_ITEM_STACK", "移动整叠"}, {"MOVE_VARIABLE_ITEM", "指定数量移动"},
    {"MOVE_ALL_ITEMS", "移动全部"}, {"ITEMS_INVENTORY", "选择背包"}, {"ITEMS_AROUND", "选择周围"},
    {"CENTER", "回到角色"}, {"zoom_in", "放大"}, {"zoom_out", "缩小"},
    {"LEVEL_UP", "上一层"}, {"LEVEL_DOWN", "下一层"}, {"LIST_NOTES", "地图笔记"},
    {"CREATE_NOTE", "添加笔记"}, {"TOGGLE_FAST_SCROLL", "光标快移"},
    {"DELETE_NOTE", "删除笔记"}, {"MARK_DANGER", "标记危险"}, {"TOGGLE_EXPLORED", "标记已探索"},
    {"TOGGLE_MAP_NOTES", "显示笔记"}, {"TOGGLE_BLINKING", "图标闪烁"},
    {"TOGGLE_OVERLAYS", "地图图层"}, {"TOGGLE_HORDES", "显示尸群"},
    {"TOGGLE_LAND_USE_CODES", "土地代号"}, {"TOGGLE_CITY_LABELS", "城市名称"},
    {"TOGGLE_MAP_REVEALS", "已揭示区域"}, {"TOGGLE_OVERMAP_WEATHER", "天气图层"},
    {"TOGGLE_FOREST_TRAILS", "林间小径"}, {"TOGGLE_FAST_TRAVEL", "旅行视图"},
    {"MISSIONS", "任务列表"},
    {"CHOOSE_DESTINATION", "设定目的地"}, {"CENTER_ON_DESTINATION", "定位目的地"},
    {"WEAR", "穿戴"}, {"WIELD", "持握"}, {"ORGANIZE_MENU", "整理背包"},
    {"CONTAIN_MODE", "卸载目标模式"}, {"TOGGLE_SKIP_UNSELECTABLE", "跳过不可选"},
    {"TOGGLE_NUMPAD_NAVIGATION", "数字导航模式"},
    {"CHOOSE_CRAFTER", "选择制作者"}, {"HIDE_SHOW_RECIPE", "隐藏恢复配方"},
    {"COMPARE", "比较物品"}, {"TOGGLE_RECIPE_UNREAD", "已读未读"},
    {"MARK_ALL_RECIPES_READ", "标记全部已读"}, {"TOGGLE_UNREAD_RECIPES_FIRST", "未读优先"},
    {"SCROLL_UP", "材料上滚"}, {"SCROLL_DOWN", "材料下滚"},
    {"TOGGLE_VEH", "地面载具"}, {"UNLOAD_CONTAINER", "卸载容器"},
    {"TOGGLE_AUTO_PICKUP", "自动拾取"},
    {"ITEMS_NW", "西北地面"}, {"ITEMS_N", "北方地面"}, {"ITEMS_NE", "东北地面"},
    {"ITEMS_W", "西方地面"}, {"ITEMS_CE", "脚下地面"}, {"ITEMS_E", "东方地面"},
    {"ITEMS_SW", "西南地面"}, {"ITEMS_S", "南方地面"}, {"ITEMS_SE", "东南地面"},
    {"ITEMS_WORN", "穿戴物品"}, {"ITEMS_DRAGGED_CONTAINER", "拖动容器"},
    {"ITEMS_CONTAINER", "选定容器"}, {"ITEMS_PARENT", "父容器"},
    {"ITEMS_DEFAULT", "默认区域"}, {"SAVE_DEFAULT", "保存默认区域"},
    {"HELP_KEYBINDINGS", "掌机按键"}, {"YES", "是"}, {"NO", "否"},
    {"TEXT.BACKSPACE", "删除"}, {"TEXT.CONFIRM", "确定"}, {"TEXT.QUIT", "取消"}, {"TEXT.LEFT", "光标左移"},
    {"TEXT.RIGHT", "光标右移"}, {"TEXT.CLEAR", "清空输入"},
};
struct panel_page_t { std::string title; std::vector<const command_t *> entries; };
struct state_t {
    bool enabled = false;
    bool menu = false;
    bool menu_used = false;
    bool keyboard = false;
    bool help = false;
    bool actions = false;
    bool caps = false;
    bool auto_keyboard = false;
    Uint32 action_event_type = static_cast<Uint32>( -1 );
    std::vector<std::string> registered;
    std::vector<panel_page_t> pages;
    bool crafting_wide = false;
    bool fast_scroll = false;
    int fast_scroll_step = 5;
    bool pending_direction = false;
    std::array<bool, 2> trigger_down{};
    Uint8 hat = SDL_HAT_CENTERED;
    Uint8 tap_hat = SDL_HAT_CENTERED;
    Uint32 due = 0;
    int selected = 0;
    int symbols = 0;
    int action_page = 0;
    int game_action_page = 0;
    int action_selected = 0;
    std::string typed;
    SDL_Joystick *joystick = nullptr;
    SDL_JoystickID joystick_id = -1;
    TTF_Font *font = nullptr;
    TTF_Font *ascii_font = nullptr;
    TTF_Font *hud_font = nullptr;
    TTF_Font *hud_ascii_font = nullptr;
    struct glyph_t { SDL_Texture *texture; int width; int height; };
    std::unordered_map<std::string, glyph_t> glyphs;
    std::string context = "MENU";
    std::string last_action;
    Uint32 action_started = 0;
    bool action_pending = false;
};
state_t state;
const char *const rows[] = {
    "1234567890", "qwertyuiop", "asdfghjkl;", "zxcvbnm,./", "!?@$%&*<>=",
};
const char *const symbol_rows[] = { "!?@$%&*<>=", ":()#_\\{}^~", "\"'`|+-[]:;" };
const char *const special_labels[] = { "空格", "大小写", "删除", "确定", "关闭", "[", "]", "-", "+", "符号" };
const char *const action_pages[] = { "行动", "物品", "信息", "战斗" };
const char *const action_labels[4][8] = {
    { "移动模式", "阅读", "屠宰", "长时间等待", "建造", "睡眠", "拆解", "重复制作" },
    { "吃喝", "穿戴", "持握", "使用物品", "投掷", "开门", "关门", "制作" },
    { "人物", "医疗", "阵营", "高级背包", "背包", "附近物品", "任务", "地图" },
    { "射击", "装填", "砸毁", "窥视", "拾取", "抓取", "控制载具", "安全模式" },
};
const char *const action_keys[] = { "\"RB|*$(-", "EWwatoc&", "@N#/iVMm", "frsXgG^!" };

char action_character()
{
    return action_keys[state.action_page][state.action_selected];
}

char selected_character( int selection )
{
    char code = selection < 40 ? rows[selection / 10][selection % 10] :
                symbol_rows[state.symbols][selection % 10];
    if( state.caps ) {
        code = static_cast<char>( std::toupper( static_cast<unsigned char>( code ) ) );
    }
    return code;
}

void expose( SDL_Event *event )
{
    *event = SDL_Event{};
    event->type = SDL_WINDOWEVENT;
    event->window.event = SDL_WINDOWEVENT_EXPOSED;
}
void key( SDL_Event *event, SDL_Keycode code )
{
    *event = SDL_Event{};
    event->type = SDL_KEYDOWN;
    event->key.state = SDL_PRESSED;
    event->key.keysym.sym = code;
    event->key.keysym.scancode = SDL_GetScancodeFromKey( code );
}
void character( SDL_Event *event, char code )
{
    *event = SDL_Event{};
    event->type = SDL_TEXTINPUT;
    event->text.text[0] = code;
    event->text.text[1] = '\0';
}
bool gameplay()
{
    return state.context == "DEFAULTMODE";
}
bool available( const std::string &action )
{
    return std::find( state.registered.begin(), state.registered.end(), action ) != state.registered.end();
}
const command_t *command( const std::string &action )
{
    for( const auto &entry : commands ) {
        if( entry.action == action ) { return &entry; }
    }
    return nullptr;
}
void clear_direction();
void logical_action( SDL_Event *event, const std::string &action )
{
    const command_t *entry = command( action );
    if( !entry || !available( action ) ) { expose( event ); return; }
    if( action == "HELP_KEYBINDINGS" ) {
        state.actions = false; state.help = true; clear_direction();
        expose( event ); return;
    }
    *event = SDL_Event{};
    event->type = state.action_event_type;
    event->user.code = action_code_base + static_cast<int>( entry - commands );
}
using controls_t = std::array<std::string, 13>;
controls_t controls()
{
    controls_t result{};
    auto choose = [&]( unsigned index, std::initializer_list<const char *> candidates ) {
        for( const char *candidate : candidates ) {
            if( available( candidate ) ) { result[index] = candidate; break; }
        }
    };
    choose( 0, {"NO", "QUIT", "UILIST.QUIT", "TEXT.QUIT"} );
    choose( 1, {"YES", "CONFIRM", "TEXT.CONFIRM"} );
    choose( 4, {"PREV_TAB", "PREV_COLUMN", "PAGE_UP"} );
    choose( 5, {"NEXT_TAB", "NEXT_COLUMN", "PAGE_DOWN"} );
    choose( 11, {"PAGE_UP", "SCROLL_RECIPE_INFO_UP", "HOME"} );
    choose( 12, {"PAGE_DOWN", "SCROLL_RECIPE_INFO_DOWN", "END"} );
    choose( 3, {"INVENTORY_FILTER", "FILTER", "SEARCH", "UILIST.FILTER"} );
    choose( 2, {"TOGGLE_ENTRY", "TOGGLE_FAVORITE"} );
    choose( 6, {"CATEGORY_SELECTION", "TOGGLE_TAB"} );
    choose( 9, {"EXAMINE", "HELP_KEYBINDINGS"} );
    choose( 10, {"SHOW_HIDE_CONTENTS", "EXAMINE_CONTENTS"} );
    if( state.context == "CRAFTING" ) {
        choose( 2, {"CYCLE_BATCH"} );
        choose( 4, {"PREV_TAB"} ); choose( 5, {"NEXT_TAB"} );
        choose( 11, {"SCROLL_RECIPE_INFO_UP"} ); choose( 12, {"SCROLL_RECIPE_INFO_DOWN"} );
        choose( 6, {"TOGGLE_FAVORITE"} );
        choose( 9, {"HELP_RECIPE"} ); choose( 10, {"RELATED_RECIPES"} );
    } else if( state.context == "ADVANCED_INVENTORY" ) {
        choose( 1, {"EXAMINE"} ); choose( 2, {"MOVE_ITEM_STACK"} );
        choose( 4, {"LEFT"} ); choose( 5, {"RIGHT"} );
        choose( 6, {"ITEMS_INVENTORY"} ); choose( 9, {"SORT"} );
    } else if( state.context == "OVERMAP" ) {
        choose( 2, {"CENTER"} ); choose( 4, {"zoom_out"} ); choose( 5, {"zoom_in"} );
        choose( 6, {"LIST_NOTES"} ); choose( 7, {"TOGGLE_FAST_SCROLL"} );
        choose( 9, {"LEVEL_UP"} ); choose( 10, {"LEVEL_DOWN"} );
        choose( 11, {"LEVEL_UP"} ); choose( 12, {"LEVEL_DOWN"} );
    } else if( state.context == "OVERMAP_SEARCH" ) {
        choose( 4, {"PREV_TAB"} ); choose( 5, {"NEXT_TAB"} );
    }
    return result;
}
void rebuild_panel()
{
    state.pages.clear();
    std::vector<std::string> used;
    auto group = [&]( const char *title, std::initializer_list<const char *> ids ) {
        std::vector<const command_t *> entries;
        for( const char *id : ids ) {
            if( available( id ) && std::find( used.begin(), used.end(), id ) == used.end() ) {
                if( const auto *entry = command( id ) ) {
                    entries.push_back( entry ); used.emplace_back( id );
                }
            }
        }
        for( size_t i = 0; i < entries.size(); i += 8 ) {
            std::string name = title;
            if( entries.size() > 8 ) { name += " " + std::to_string( i / 8 + 1 ); }
            state.pages.push_back( {name, {entries.begin() + i,
                                         entries.begin() + std::min( i + 8, entries.size() )}} );
        }
    };
    if( state.context == "OVERMAP" ) {
        group( "浏览地图", {"TOGGLE_FAST_SCROLL", "SEARCH", "CENTER", "zoom_out", "zoom_in",
                             "LEVEL_UP", "LEVEL_DOWN", "LIST_NOTES"} );
        group( "标记与路线", {"CREATE_NOTE", "DELETE_NOTE", "MARK_DANGER", "TOGGLE_EXPLORED",
                                "CHOOSE_DESTINATION", "CENTER_ON_DESTINATION", "MISSIONS"} );
        group( "地图显示", {"TOGGLE_MAP_NOTES", "TOGGLE_BLINKING", "TOGGLE_OVERLAYS", "TOGGLE_HORDES",
                              "TOGGLE_LAND_USE_CODES", "TOGGLE_CITY_LABELS", "TOGGLE_MAP_REVEALS",
                              "TOGGLE_OVERMAP_WEATHER", "TOGGLE_FOREST_TRAILS", "TOGGLE_FAST_TRAVEL"} );
    } else if( state.context == "CRAFTING" ) {
        group( "常用制作", {"FILTER", "RESET_FILTER", "TOGGLE_FAVORITE", "CYCLE_BATCH", "HELP_RECIPE",
                              "RELATED_RECIPES", "CHOOSE_CRAFTER", "COMPARE"} );
        group( "右侧材料", {"SCROLL_RECIPE_INFO_UP", "SCROLL_RECIPE_INFO_DOWN"} );
        if( state.crafting_wide ) {
            group( "成品说明", {"SCROLL_ITEM_INFO_UP", "SCROLL_ITEM_INFO_DOWN"} );
        }
        group( "配方列表", {"PAGE_UP", "PAGE_DOWN", "HOME", "END", "PREV_TAB", "NEXT_TAB"} );
        group( "配方整理", {"HIDE_SHOW_RECIPE", "TOGGLE_RECIPE_UNREAD", "MARK_ALL_RECIPES_READ",
                              "TOGGLE_UNREAD_RECIPES_FIRST"} );
    } else if( state.context == "ADVANCED_INVENTORY" ) {
        group( "搬运物品", {"FILTER", "RESET_FILTER", "EXAMINE", "SORT", "MOVE_SINGLE_ITEM",
                              "MOVE_ITEM_STACK", "MOVE_VARIABLE_ITEM", "MOVE_ALL_ITEMS"} );
        group( "物品区域", {"ITEMS_INVENTORY", "ITEMS_WORN", "ITEMS_CE", "ITEMS_AROUND",
                              "ITEMS_CONTAINER", "ITEMS_DRAGGED_CONTAINER", "ITEMS_PARENT", "TOGGLE_VEH"} );
        // The eight wheel positions also describe the eight surrounding ground tiles.
        group( "周围地面", {"ITEMS_N", "ITEMS_NE", "ITEMS_E", "ITEMS_SE", "ITEMS_S",
                              "ITEMS_SW", "ITEMS_W", "ITEMS_NW"} );
        group( "列表与设置", {"PAGE_UP", "PAGE_DOWN", "HOME", "END", "TOGGLE_TAB", "ITEMS_DEFAULT",
                                "SAVE_DEFAULT", "TOGGLE_AUTO_PICKUP", "UNLOAD_CONTAINER"} );
    } else {
        group( "常用操作", {"INVENTORY_FILTER", "FILTER", "SEARCH", "UILIST.FILTER", "RESET_FILTER",
                              "TOGGLE_ENTRY", "TOGGLE_FAVORITE", "EXAMINE"} );
        group( "容器与分类", {"SHOW_HIDE_CONTENTS", "SHOW_HIDE_CONTENTS_ALL", "EXAMINE_CONTENTS",
                                "CATEGORY_SELECTION", "VIEW_CATEGORY_MODE", "CONTAIN_MODE",
                                "ORGANIZE_MENU", "TOGGLE_SKIP_UNSELECTABLE"} );
        group( "列表浏览", {"PAGE_UP", "PAGE_DOWN", "HOME", "END", "PREV_COLUMN", "NEXT_COLUMN",
                              "PREV_TAB", "NEXT_TAB"} );
        group( "数量与装备", {"MARK_WITH_COUNT", "INCREASE_COUNT", "DECREASE_COUNT", "WEAR", "WIELD"} );
    }
    group( "其他操作", {"HELP_KEYBINDINGS"} );
    for( auto &page : state.pages ) {
        if( page.title == "右侧材料" || page.title == "成品说明" ||
            page.title == "配方列表" || page.title == "列表浏览" ) {
            const auto original = page.entries;
            page.entries.assign( 8, nullptr );
            auto place = [&]( int position, std::initializer_list<const char *> ids ) {
                for( const char *id : ids ) {
                    const auto found = std::find_if( original.begin(), original.end(),
                        [&]( const command_t *entry ) { return entry->action == id; } );
                    if( found != original.end() ) { page.entries[position] = *found; break; }
                }
            };
            place( 0, {"SCROLL_RECIPE_INFO_UP", "SCROLL_ITEM_INFO_UP", "PAGE_UP"} );
            place( 4, {"SCROLL_RECIPE_INFO_DOWN", "SCROLL_ITEM_INFO_DOWN", "PAGE_DOWN"} );
            place( 6, {"PREV_COLUMN", "PREV_TAB"} ); place( 2, {"NEXT_COLUMN", "NEXT_TAB"} );
            place( 7, {"HOME"} ); place( 1, {"END"} );
            if( available( "PREV_COLUMN" ) ) {
                place( 5, {"PREV_TAB"} ); place( 3, {"NEXT_TAB"} );
            }
        }
    }
    if( state.pages.empty() ) { state.pages.push_back( {"当前菜单", {}} ); }
    if( !gameplay() ) { state.action_page = std::min( state.action_page, static_cast<int>( state.pages.size() ) - 1 ); }
}
int panel_page_count()
{
    return gameplay() ? 4 : std::max( 1, static_cast<int>( state.pages.size() ) );
}
const command_t *selected_panel_command()
{
    if( state.pages.empty() ) { return nullptr; }
    const auto &entries = state.pages[state.action_page].entries;
    return state.action_selected < static_cast<int>( entries.size() ) ? entries[state.action_selected] : nullptr;
}
std::string command_label( const command_t *entry )
{
    if( !entry ) { return "—"; }
    if( entry->action == "TOGGLE_FAST_SCROLL" ) {
        return std::string( "光标快移:" ) + ( state.fast_scroll ? "开" : "关" );
    }
    return entry->label;
}
std::string panel_purpose()
{
    if( gameplay() && action_character() == '"' ) { return "移动模式：走 / 跑 / 蹲；每次仍走一格"; }
    if( gameplay() ) { return "START保存退出；" + std::string( action_labels[state.action_page][state.action_selected] ); }
    const auto *entry = selected_panel_command();
    if( !entry ) { return "方向选动作；空位没有操作"; }
    if( entry->action == "TOGGLE_FAST_SCROLL" ) {
        return "光标每次 " + std::to_string( state.fast_scroll ? state.fast_scroll_step : 1 ) +
               " 格；仅大地图光标，人物不移动";
    }
    if( entry->action.find( "SCROLL_RECIPE_INFO" ) == 0 ) { return "滚动右侧材料；左侧选中配方保持不变"; }
    if( entry->action.find( "SCROLL_ITEM_INFO" ) == 0 ) { return "滚动成品说明；仅在成品栏可见时使用"; }
    if( entry->action == "PAGE_UP" || entry->action == "PAGE_DOWN" ) {
        return state.context == "CRAFTING" ? "翻动左侧配方列表；L2/R2滚动右侧材料" : "翻动当前物品列表";
    }
    if( state.context == "OVERMAP" && entry->action.find( "TOGGLE_" ) == 0 ) {
        return "切换显示：" + command_label( entry );
    }
    return "当前操作：" + command_label( entry );
}
void clear_direction()
{
    state.hat = SDL_HAT_CENTERED;
    state.tap_hat = SDL_HAT_CENTERED;
    state.pending_direction = false;
    state.due = 0;
}
void toggle_keyboard( SDL_Event *event )
{
    state.keyboard = !state.keyboard;
    state.auto_keyboard = false;
    state.help = false;
    state.actions = false;
    state.typed.clear();
    clear_direction();
    expose( event );
}
void keyboard_key( SDL_Event *event, int selection )
{
    if( selection == 59 ) {
        state.symbols = ( state.symbols + 1 ) % 3;
        expose( event );
    } else if( selection == 51 ) {
        state.caps = !state.caps;
        expose( event );
    } else if( selection == 52 ) {
        if( !state.typed.empty() ) {
            state.typed.pop_back();
        }
        if( available( "TEXT.BACKSPACE" ) ) { logical_action( event, "TEXT.BACKSPACE" ); }
        else { key( event, SDLK_BACKSPACE ); }
    } else if( selection == 53 ) {
        state.keyboard = false;
        clear_direction();
        if( available( "TEXT.CONFIRM" ) ) { logical_action( event, "TEXT.CONFIRM" ); }
        else { key( event, SDLK_RETURN ); }
    } else if( selection == 54 ) {
        state.keyboard = false;
        clear_direction();
        expose( event );
    } else {
        char code;
        if( selection < 50 ) {
            code = selected_character( selection );
        } else {
            code = selection == 50 ? ' ' : special_labels[selection - 50][0];
        }
        state.typed += code;
        character( event, code );
    }
}
void direction( SDL_Event *event, Uint8 hat )
{
    if( state.actions ) {
        switch( hat ) {
            case SDL_HAT_UP: state.action_selected = 0; break;
            case SDL_HAT_RIGHTUP: state.action_selected = 1; break;
            case SDL_HAT_RIGHT: state.action_selected = 2; break;
            case SDL_HAT_RIGHTDOWN: state.action_selected = 3; break;
            case SDL_HAT_DOWN: state.action_selected = 4; break;
            case SDL_HAT_LEFTDOWN: state.action_selected = 5; break;
            case SDL_HAT_LEFT: state.action_selected = 6; break;
            case SDL_HAT_LEFTUP: state.action_selected = 7; break;
            default: break;
        }
        expose( event );
        return;
    }
    if( state.help ) {
        expose( event );
        return;
    }
    if( state.keyboard ) {
        int row = state.selected / 10;
        int col = state.selected % 10;
        if( hat & SDL_HAT_UP ) {
            row = ( row + 5 ) % 6;
        } else if( hat & SDL_HAT_DOWN ) {
            row = ( row + 1 ) % 6;
        } else if( hat & SDL_HAT_LEFT ) {
            col = ( col + 9 ) % 10;
        } else if( hat & SDL_HAT_RIGHT ) {
            col = ( col + 1 ) % 10;
        }
        state.selected = row * 10 + col;
        expose( event );
        return;
    }
    if( state.menu && !gameplay() ) {
        state.menu_used = true;
        const char *action = hat & SDL_HAT_UP ? "PAGE_UP" : hat & SDL_HAT_DOWN ? "PAGE_DOWN" :
                             hat & SDL_HAT_LEFT ? "HOME" : "END";
        logical_action( event, action );
        return;
    }
    if( state.menu ) {
        state.menu_used = true;
        if( hat & SDL_HAT_UP ) {
            character( event, 'z' );
        } else if( hat & SDL_HAT_DOWN ) {
            character( event, 'Z' );
        } else if( hat & SDL_HAT_LEFT ) {
            character( event, '<' );
        } else {
            character( event, '>' );
        }
        return;
    }
    if( !gameplay() && !state.registered.empty() ) {
        const char *action = nullptr;
        switch( hat ) {
            case SDL_HAT_LEFTUP: action = "LEFTUP"; break;
            case SDL_HAT_RIGHTUP: action = "RIGHTUP"; break;
            case SDL_HAT_LEFTDOWN: action = "LEFTDOWN"; break;
            case SDL_HAT_RIGHTDOWN: action = "RIGHTDOWN"; break;
            default: break;
        }
        if( action && available( action ) ) { logical_action( event, action ); return; }
        if( hat & SDL_HAT_UP ) { action = "UP"; }
        else if( hat & SDL_HAT_DOWN ) { action = "DOWN"; }
        else if( hat & SDL_HAT_LEFT ) { action = available( "PREV_COLUMN" ) ? "PREV_COLUMN" : "LEFT"; }
        else { action = available( "NEXT_COLUMN" ) ? "NEXT_COLUMN" : "RIGHT"; }
        if( available( action ) ) { logical_action( event, action ); return; }
        const std::string list_action = std::string( "UILIST." ) + action;
        if( available( list_action ) ) { logical_action( event, list_action ); return; }
        // Unknown menus still accept native arrows, never diagonal item invlets.
        key( event, hat & SDL_HAT_UP ? SDLK_UP : hat & SDL_HAT_DOWN ? SDLK_DOWN :
             hat & SDL_HAT_LEFT ? SDLK_LEFT : SDLK_RIGHT );
        return;
    }
    switch( hat ) {
        case SDL_HAT_UP: key( event, SDLK_UP ); break;
        case SDL_HAT_DOWN: key( event, SDLK_DOWN ); break;
        case SDL_HAT_LEFT: key( event, SDLK_LEFT ); break;
        case SDL_HAT_RIGHT: key( event, SDLK_RIGHT ); break;
        case SDL_HAT_LEFTUP: character( event, '7' ); break;
        case SDL_HAT_RIGHTUP: character( event, '9' ); break;
        case SDL_HAT_LEFTDOWN: character( event, '1' ); break;
        case SDL_HAT_RIGHTDOWN: character( event, '3' ); break;
        default: expose( event ); break;
    }
}
void button( SDL_Event *event, unsigned index )
{
    if( state.actions ) {
        state.menu_used = true;
        if( index == 1 ) {
            const char selected = gameplay() ? action_character() : '\0';
            state.actions = false;
            clear_direction();
            if( gameplay() ) { character( event, selected ); }
            else {
                const command_t *entry = selected_panel_command();
                if( entry ) { logical_action( event, entry->action ); }
                else { expose( event ); }
            }
        } else if( index == 0 ) {
            state.actions = false;
            clear_direction();
            expose( event );
        } else if( index == 4 || index == 5 || index == 11 || index == 12 ) {
            const int pages = panel_page_count();
            state.action_page = ( state.action_page + ( index == 4 || index == 11 ? pages - 1 : 1 ) ) % pages;
            expose( event );
        } else if( index == 6 ) {
            toggle_keyboard( event );
        } else if( index == 3 ) {
            state.actions = false;
            state.help = true;
            clear_direction();
            expose( event );
        } else if( index == 7 ) {
            state.actions = false;
            clear_direction();
            if( gameplay() ) { character( event, 'S' ); }
            else { expose( event ); }
        } else {
            expose( event );
        }
        return;
    }
    if( state.keyboard ) {
        if( index == 1 ) {
            keyboard_key( event, state.selected );
        } else if( index == 0 ) {
            if( state.auto_keyboard ) {
                state.keyboard = state.auto_keyboard = false;
                clear_direction();
                logical_action( event, "TEXT.QUIT" );
            } else { keyboard_key( event, 54 ); }
        } else if( index == 3 ) {
            keyboard_key( event, 52 );
        } else if( index == 2 ) {
            keyboard_key( event, 51 );
        } else if( index == 7 ) {
            keyboard_key( event, 53 );
        } else if( index == 6 ) {
            keyboard_key( event, 50 );
        } else if( index == 4 || index == 5 ) {
            if( available( "TEXT.LEFT" ) ) {
                logical_action( event, index == 4 ? "TEXT.LEFT" : "TEXT.RIGHT" );
            } else { expose( event ); }
        } else if( index == 9 && available( "TEXT.CLEAR" ) ) {
            state.typed.clear();
            logical_action( event, "TEXT.CLEAR" );
        } else if( index == 11 && available( "TEXT.CLEAR" ) ) {
            state.typed.clear();
            logical_action( event, "TEXT.CLEAR" );
        } else if( index == 12 ) {
            state.symbols = ( state.symbols + 1 ) % 3;
            state.selected = 40;
            expose( event );
        } else {
            expose( event );
        }
        state.menu_used = true;
        return;
    }
    if( state.help ) {
        if( index == 0 || index == 1 ) {
            state.help = false;
            clear_direction();
        }
        expose( event );
        return;
    }
    if( !gameplay() ) {
        if( state.menu ) {
            state.menu_used = true;
            if( index == 6 ) { toggle_keyboard( event ); return; }
            if( index == 3 ) { logical_action( event, "RESET_FILTER" ); return; }
            if( index == 2 ) { logical_action( event, "TOGGLE_FAVORITE" ); return; }
            if( index == 9 ) { logical_action( event, "DECREASE_COUNT" ); return; }
            if( index == 10 ) { logical_action( event, "INCREASE_COUNT" ); return; }
            if( index != 0 && index != 1 ) { expose( event ); return; }
        }
        const auto bindings = controls();
        if( index == 7 && bindings[index].empty() ) {
            // START opens the contextual panel; it never cycles the same tab as R.
            state.actions = true;
            state.action_page = state.action_selected = 0;
            clear_direction();
            expose( event );
            return;
        }
        if( index < bindings.size() && !bindings[index].empty() ) {
            logical_action( event, bindings[index] );
        } else if( index == 0 || index == 1 || index == 7 ) {
            key( event, index == 0 ? SDLK_ESCAPE : index == 1 ? SDLK_RETURN : SDLK_TAB );
        } else { expose( event ); }
        return;
    }
    if( state.menu ) {
        state.menu_used = true;
        switch( index ) {
            case 0: character( event, 'N' ); break;
            case 1: character( event, 'Y' ); break;
            case 2: character( event, '$' ); break;
            case 3: character( event, 'e' ); break;
            case 4: character( event, 's' ); break;
            case 5: character( event, 'd' ); break;
            case 6: toggle_keyboard( event ); break;
            case 7: character( event, 'S' ); break; // Save and quit in the pinned bindings.
            case 9: character( event, '&' ); break;
            case 10: character( event, '@' ); break;
            case 11: character( event, '&' ); break;
            case 12: character( event, '/' ); break;
            default: expose( event ); break;
        }
    } else {
        switch( index ) {
            case 0: key( event, SDLK_ESCAPE ); break;
            case 1: key( event, SDLK_RETURN ); break;
            case 2: character( event, '5' ); break;
            case 3: character( event, 'g' ); break;
            case 4: character( event, 'f' ); break;
            case 5: character( event, 'r' ); break;
            case 6: character( event, 'i' ); break;
            case 7: key( event, SDLK_TAB ); break;
            case 9: character( event, ';' ); break;
            case 10: character( event, 'm' ); break;
            case 11: character( event, '&' ); break;
            case 12: character( event, '/' ); break;
            default: expose( event ); break;
        }
    }
}
Uint32 decode( const std::string &text, size_t &i )
{
    const auto lead = static_cast<unsigned char>( text[i++] );
    Uint32 value = lead;
    unsigned count = 0;
    if( lead >= 0xf0 ) { value = lead & 7; count = 3; }
    else if( lead >= 0xe0 ) { value = lead & 15; count = 2; }
    else if( lead >= 0xc0 ) { value = lead & 31; count = 1; }
    while( count > 0 && i < text.size() ) {
        --count;
        value = ( value << 6 ) | ( static_cast<unsigned char>( text[i++] ) & 63 );
    }
    return value;
}
int cells( Uint32 c )
{
    return ( c >= 0x1100 && ( c <= 0x115f || c == 0x2329 || c == 0x232a ||
             ( c >= 0x2e80 && c <= 0xa4cf ) || ( c >= 0xac00 && c <= 0xd7a3 ) ||
             ( c >= 0xf900 && c <= 0xfaff ) || ( c >= 0xfe10 && c <= 0xfe6f ) ||
             ( c >= 0xff01 && c <= 0xff60 ) || ( c >= 0xffe0 && c <= 0xffe6 ) ||
             ( c >= 0x1f300 && c <= 0x1faff ) || ( c >= 0x20000 && c <= 0x3ffff ) ) ) ? 2 : 1;
}
void label( SDL_Renderer *renderer, const std::string &text, int x, int y, bool hud = false )
{
    const int cell_width = hud ? 12 : 14;
    const int cell_height = hud ? 24 : 32;
    for( size_t i = 0; i < text.size(); ) {
        const size_t begin = i;
        const Uint32 c = decode( text, i );
        const std::string glyph = text.substr( begin, i - begin );
        const int advance = cells( c ) * cell_width;
        const std::string key = ( hud ? "h" : "p" ) + glyph;
        auto found = state.glyphs.find( key );
        if( found == state.glyphs.end() ) {
            TTF_Font *font = c < 128 ? ( hud ? state.hud_ascii_font : state.ascii_font ) :
                             ( hud ? state.hud_font : state.font );
            SDL_Surface *surface = TTF_RenderUTF8_Blended( font, glyph.c_str(), {235,239,245,255} );
            if( surface ) {
                state_t::glyph_t value{ SDL_CreateTextureFromSurface( renderer, surface ), surface->w, surface->h };
                SDL_FreeSurface( surface );
                found = state.glyphs.emplace( key, value ).first;
            }
        }
        if( found != state.glyphs.end() && found->second.texture ) {
            const auto &g = found->second;
            const int w = std::min( g.width, advance );
            const int h = std::min( g.height, cell_height );
            SDL_Rect src{ (g.width-w)/2, (g.height-h)/2, w, h };
            SDL_Rect dst{ x+(advance-w)/2, y+(cell_height-h)/2, w, h };
            SDL_RenderCopy( renderer, g.texture, &src, &dst );
        }
        x += advance;
    }
}
void hud( SDL_Renderer *renderer, int width, int height )
{
    SDL_SetRenderDrawColor( renderer, 12, 18, 28, 255 );
    SDL_Rect area{ 0, height - 48, width, 48 };
    SDL_RenderFillRect( renderer, &area );
    label( renderer, control_hint( false ), 10, area.y, true );
    label( renderer, control_hint( true ), 10, area.y + 24, true );
}
} // namespace

void init()
{
    shutdown();
    const char *enabled = std::getenv( "CDDA_BRICK" );
    state.enabled = enabled && std::strcmp( enabled, "1" ) == 0;
    if( !state.enabled ) {
        return;
    }
    rebuild_panel();
    state.action_event_type = SDL_RegisterEvents( 1 );
    if( state.action_event_type == static_cast<Uint32>( -1 ) ) {
        std::fprintf( stderr, "Brick action event allocation failed\n" );
        state.enabled = false;
        return;
    }
    if( SDL_InitSubSystem( SDL_INIT_JOYSTICK ) != 0 ) {
        std::fprintf( stderr, "Brick joystick init: %s\n", SDL_GetError() );
    }
    if( SDL_NumJoysticks() > 0 ) {
        state.joystick = SDL_JoystickOpen( 0 );
        if( state.joystick ) {
            state.joystick_id = SDL_JoystickInstanceID( state.joystick );
        }
    }
    std::fprintf( stderr, "Brick input enabled; joystick=%s\n",
                  state.joystick ? SDL_JoystickName( state.joystick ) : "none (keyboard available)" );
}

void shutdown()
{
    if( state.font ) {
        TTF_CloseFont( state.font );
    }
    for( const auto &glyph : state.glyphs ) { SDL_DestroyTexture( glyph.second.texture ); }
    if( state.ascii_font ) { TTF_CloseFont( state.ascii_font ); }
    if( state.hud_font ) { TTF_CloseFont( state.hud_font ); }
    if( state.hud_ascii_font ) { TTF_CloseFont( state.hud_ascii_font ); }
    if( state.joystick ) {
        SDL_JoystickClose( state.joystick );
    }
    if( state.enabled ) {
        SDL_QuitSubSystem( SDL_INIT_JOYSTICK );
    }
    state = state_t{};
}

int poll_event( SDL_Event *event )
{
    if( !state.enabled || !event ) {
        return SDL_PollEvent( event );
    }
    while( SDL_PollEvent( event ) ) {
        const Uint32 now = SDL_GetTicks();
        if( event->type == SDL_JOYDEVICEADDED ) {
            if( !state.joystick ) {
                state.joystick = SDL_JoystickOpen( event->jdevice.which );
                if( state.joystick ) {
                    state.joystick_id = SDL_JoystickInstanceID( state.joystick );
                }
            }
            continue;
        }
        if( event->type == SDL_JOYDEVICEREMOVED ) {
            if( state.joystick && event->jdevice.which == state.joystick_id ) {
                SDL_JoystickClose( state.joystick );
                state.joystick = nullptr;
                state.joystick_id = -1;
                state.menu = state.menu_used = false;
                state.keyboard = state.help = state.actions = false;
                clear_direction();
            }
            continue;
        }
        if( event->type == SDL_JOYHATMOTION ) {
            if( state.joystick && !SDL_JoystickGetAttached( state.joystick ) ) {
                continue;
            }
            if( state.joystick_id >= 0 && event->jhat.which != state.joystick_id ) {
                continue;
            }
            if( event->jhat.value == SDL_HAT_CENTERED ) {
                const Uint8 previous = state.tap_hat;
                const bool pending = state.pending_direction;
                clear_direction();
                if( pending && previous != SDL_HAT_CENTERED ) {
                    direction( event, previous );
                    return 1;
                }
            } else if( event->jhat.value != state.hat ) {
                // Removing one half of a diagonal must not add a cardinal step
                // when the other half is released a few milliseconds later.
                const bool releasing_part = state.hat != SDL_HAT_CENTERED &&
                                            ( event->jhat.value & state.hat ) == event->jhat.value;
                if( !releasing_part ) {
                    state.tap_hat = event->jhat.value;
                }
                state.hat = event->jhat.value;
                state.pending_direction = true;
                state.due = now + diagonal_delay;
            }
            continue;
        }
        if( event->type == SDL_JOYAXISMOTION ) {
            // tg5040 exposes L2/R2 as ABS_Z / ABS_RZ (SDL axes 2 / 5),
            // rather than joystick buttons. Rising edges fire exactly once.
            if( ( state.joystick_id >= 0 && event->jaxis.which != state.joystick_id ) ||
                ( event->jaxis.axis != 2 && event->jaxis.axis != 5 ) ) { continue; }
            if( state.joystick && !SDL_JoystickGetAttached( state.joystick ) ) { continue; }
            const unsigned trigger = event->jaxis.axis == 2 ? 0 : 1;
            if( event->jaxis.value <= 8192 ) { state.trigger_down[trigger] = false; }
            if( event->jaxis.value >= 16384 && !state.trigger_down[trigger] ) {
                state.trigger_down[trigger] = true;
                button( event, 11 + trigger );
                return 1;
            }
            continue;
        }
        if( event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP ) {
            if( state.joystick && !SDL_JoystickGetAttached( state.joystick ) ) {
                continue;
            }
            if( state.joystick_id >= 0 && event->jbutton.which != state.joystick_id ) {
                continue;
            }
            const unsigned index = event->jbutton.button;
            if( index == menu_button ) {
                if( event->type == SDL_JOYBUTTONDOWN ) {
                    state.menu = true;
                    state.menu_used = false;
                    expose( event );
                    return 1;
                }
                if( !state.menu ) {
                    continue;
                }
                if( state.pending_direction ) {
                    const bool pending = state.tap_hat != SDL_HAT_CENTERED;
                    if( pending ) {
                        direction( event, state.tap_hat );
                    }
                    state.menu = false;
                    clear_direction();
                    if( pending ) {
                        return 1;
                    }
                    continue;
                }
                state.menu = false;
                clear_direction();
                if( !state.menu_used ) {
                    if( state.keyboard ) {
                        state.keyboard = false;
                    } else if( state.help ) {
                        state.help = false;
                    } else {
                        state.actions = !state.actions;
                        state.action_page %= panel_page_count();
                    }
                    expose( event );
                    return 1;
                }
                expose( event );
                return 1;
            }
            if( event->type == SDL_JOYBUTTONUP ) {
                continue;
            }
            button( event, index );
            return 1;
        }
        // Disable the upstream controller path: raw Brick events are translated above.
        if( event->type >= SDL_CONTROLLERAXISMOTION && event->type <= SDL_CONTROLLERDEVICEREMAPPED ) {
            continue;
        }
        if( event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_FOCUS_LOST ) {
            state.menu = state.menu_used = state.keyboard = state.help = state.actions = false;
            clear_direction();
        }
        if( event->type == SDL_KEYDOWN ) {
            const SDL_Keycode sym = event->key.keysym.sym;
            if( sym == SDLK_F5 ) {
                if( !event->key.repeat ) {
                    toggle_keyboard( event );
                } else {
                    expose( event );
                }
                return 1;
            }
            if( state.actions ) {
                if( sym == SDLK_RETURN ) {
                    button( event, 1 );
                } else if( sym == SDLK_ESCAPE ) {
                    button( event, 0 );
                } else if( sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_LEFT || sym == SDLK_RIGHT ) {
                    direction( event, sym == SDLK_UP ? SDL_HAT_UP : sym == SDLK_DOWN ? SDL_HAT_DOWN :
                               sym == SDLK_LEFT ? SDL_HAT_LEFT : SDL_HAT_RIGHT );
                } else {
                    expose( event );
                }
            } else if( state.keyboard ) {
                if( sym == SDLK_RETURN ) {
                    keyboard_key( event, state.selected );
                } else if( sym == SDLK_ESCAPE ) {
                    button( event, 0 );
                } else if( sym == SDLK_BACKSPACE ) {
                    keyboard_key( event, 52 );
                } else if( sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_LEFT || sym == SDLK_RIGHT ) {
                    direction( event, sym == SDLK_UP ? SDL_HAT_UP : sym == SDLK_DOWN ? SDL_HAT_DOWN :
                               sym == SDLK_LEFT ? SDL_HAT_LEFT : SDL_HAT_RIGHT );
                }
            } else if( state.help ) {
                if( sym == SDLK_RETURN || sym == SDLK_ESCAPE ) {
                    state.help = false;
                }
                expose( event );
            }
        }
        if( ( state.help || state.actions ) && event->type == SDL_TEXTINPUT ) {
            expose( event );
        }
        return 1;
    }
    const Uint32 now = SDL_GetTicks();
    if( state.hat != SDL_HAT_CENTERED && static_cast<Sint32>( now - state.due ) >= 0 ) {
        const bool slow = gameplay() && !state.keyboard && !state.actions && !state.help;
        state.due = now + ( state.pending_direction ? ( slow ? repeat_delay : menu_repeat_delay ) :
                           ( slow ? repeat_interval : menu_repeat_interval ) );
        state.pending_direction = false;
        state.tap_hat = SDL_HAT_CENTERED;
        direction( event, state.hat );
        return 1;
    }
    return 0;
}

// Wait without removing raw events: the adapter remains the only consumer.
// Held directions need an independent deadline even when SDL has no new event.
void reset_after_suspend()
{
    // Keep the active menu/keyboard, but discard held buttons and queued input.
    state.menu = state.menu_used = false;
    clear_direction();
    SDL_PumpEvents();
    SDL_FlushEvents( SDL_KEYDOWN, SDL_KEYUP );
    SDL_FlushEvent( SDL_TEXTINPUT );
    SDL_FlushEvents( SDL_JOYAXISMOTION, SDL_JOYBUTTONUP );
    SDL_FlushEvents( SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERBUTTONUP );
    SDL_FlushEvent( state.action_event_type );
    SDL_SetModState( KMOD_NONE );
}
void wait_for_event( int maximum_ms )
{
    if( !state.enabled ) {
        SDL_Delay( 1 );
        return;
    }
    int timeout = std::max( 0, std::min( maximum_ms, 25 ) );
    if( state.hat != SDL_HAT_CENTERED ) {
        const Sint32 remaining = static_cast<Sint32>( state.due - SDL_GetTicks() );
        timeout = std::min( timeout, std::max( 0, static_cast<int>( remaining ) ) );
    }
    if( timeout > 0 ) {
        SDL_WaitEventTimeout( nullptr, timeout );
    }
}

static void trace( const char *kind, const std::string &detail, Uint32 elapsed = 0 )
{
    const char *path = std::getenv( "CDDA_BRICK_TRACE_PATH" );
    if( !state.enabled || !path || !*path ) {
        return;
    }
    // Only menu transitions, menu-opening actions and slow processing are
    // written. Keep at most two 64 KiB files; never log typed text or item names.
    FILE *file = std::fopen( path, "ab" );
    if( !file ) { return; }
    if( std::fseek( file, 0, SEEK_END ) == 0 && std::ftell( file ) >= 65536 ) {
        std::fclose( file );
        std::rename( path, ( std::string( path ) + ".previous" ).c_str() );
        file = std::fopen( path, "wb" );
        if( !file ) { return; }
    }
    long rss_kib = 0;
    FILE *memory = std::fopen( "/proc/self/status", "r" );
    if( memory ) {
        char line[256];
        while( std::fgets( line, sizeof( line ), memory ) ) {
            if( std::sscanf( line, "VmRSS: %ld", &rss_kib ) == 1 ) { break; }
        }
        std::fclose( memory );
    }
    std::fprintf( file, "%u %s context=%s detail=%s elapsed_ms=%u rss_kib=%ld\n",
                  SDL_GetTicks(), kind, state.context.c_str(), detail.c_str(), elapsed, rss_kib );
    std::fclose( file );
}

void note_suspend( const std::string &phase )
{
    trace( "SUSPEND", phase );
}
void note_action( const std::string &action )
{
    if( !state.enabled ) { return; }
    state.last_action = action;
    state.action_started = SDL_GetTicks();
    state.action_pending = action != "TIMEOUT" && action != "ERROR";
    // Opt-in validation trace records resolved action IDs, never typed text.
    const char *all_actions = std::getenv( "CDDA_BRICK_TRACE_ACTIONS" );
    if( state.action_pending && all_actions && std::strcmp( all_actions, "1" ) == 0 ) {
        trace( "ACTION", action );
    }
    if( state.context == "DEFAULTMODE" &&
        ( action == "action_menu" || action == "inventory" || action == "craft" ||
          action == "long_craft" || action == "advinv" || action == "item_action_menu" ) ) {
        trace( "ACTION_BEGIN", action );
    }
}

void begin_input_wait()
{
    if( state.enabled && state.action_pending ) {
        const Uint32 elapsed = SDL_GetTicks() - state.action_started;
        if( elapsed >= 1000 ) { trace( "SLOW_PROCESSING", state.last_action, elapsed ); }
        state.action_pending = false;
    }
}

int reserved_height()
{
    const char *enabled = std::getenv( "CDDA_BRICK" );
    return enabled && std::strcmp( enabled, "1" ) == 0 ? 48 : 0;
}
void set_context( const std::string &context, const std::vector<std::string> &actions )
{
    if( state.context != context || state.registered != actions ) {
        const bool changed = state.context != context;
        trace( "CONTEXT", context, state.action_pending ? SDL_GetTicks() - state.action_started : 0 );
        if( state.context == "DEFAULTMODE" ) { state.game_action_page = state.action_page; }
        state.context = context;
        state.registered = actions;
        // A held direction must not leak out of a menu into character movement.
        clear_direction();
        state.actions = state.help = false;
        state.action_page = gameplay() ? state.game_action_page : 0;
        state.action_selected = 0;
        rebuild_panel();
        if( changed ) {
            state.keyboard = state.auto_keyboard = false;
            state.typed.clear();
            if( context == "STRING_INPUT" && available( "TEXT.CONFIRM" ) ) {
                state.keyboard = state.auto_keyboard = true;
            }
        }
        if( state.enabled ) {
            SDL_Event event{};
            expose( &event );
            SDL_PushEvent( &event );
        }
    }
}
void set_crafting_view( bool wide )
{
    if( state.crafting_wide != wide ) {
        state.crafting_wide = wide;
        if( state.context == "CRAFTING" ) { rebuild_panel(); }
    }
}
void set_overmap_cursor_mode( bool fast, int step )
{
    state.fast_scroll = fast;
    state.fast_scroll_step = step;
}
int event_action_code( const SDL_Event &event )
{
    return state.enabled && event.type == state.action_event_type &&
           !mapped_action( event.user.code ).empty() ? event.user.code : 0;
}
const std::string &mapped_action( int code )
{
    static const std::string empty;
    const int index = code - action_code_base;
    return state.enabled && index >= 0 && index < static_cast<int>( sizeof( commands ) / sizeof( commands[0] ) ) ?
           commands[index].action : empty;
}
static std::string control_hint_core( bool second_line )
{
    if( state.keyboard ) {
        return second_line ? ( state.auto_keyboard ? "START确定 B取消 SELECT空格 L/R光标 L2清空 R2符号" :
                               "START确定 B/MENU收起 SELECT空格" ) : "方向选择 A输入 X删除 Y大小写";
    }
    if( state.actions ) {
        return second_line ? panel_purpose() : "方向选动作 A执行 B返回 L/R换组 SELECT键盘 X帮助";
    }
    if( state.help ) { return second_line ? "点按MENU动作面板 MENU+SELECT键盘" : "A/B/MENU收起帮助"; }
    if( state.menu && gameplay() ) {
        return second_line ? "MENU+FN1制作 FN2人物 上/下缩放 左/右楼层" :
               "MENU+A是 B否 X检查 Y睡眠 L砸毁 R丢弃 SELECT键盘 START保存";
    }
    if( state.menu ) {
        std::string hint;
        const auto bindings = controls();
        if( !second_line ) {
            for( unsigned index : {1u, 0u} ) {
                const command_t *entry = command( bindings[index] );
                if( entry ) { hint += std::string( "MENU+" ) + ( index ? "A" : "B" ) + entry->label + " "; }
            }
            if( available( "RESET_FILTER" ) ) { hint += "X清除筛选 "; }
            if( available( "TOGGLE_FAVORITE" ) ) { hint += "Y收藏 "; }
            hint += "SELECT键盘";
        } else {
            if( available( "PAGE_UP" ) ) { hint += "MENU+上/下翻页 "; }
            if( available( "HOME" ) ) { hint += "左/右首末 "; }
            if( available( "INCREASE_COUNT" ) ) { hint += "FN1/FN2数量-/+ "; }
            hint += "松开MENU动作面板";
        }
        return hint;
    }
    if( gameplay() ) {
        return second_line ? "L射击 R装填 L2制作 R2高级背包 FN1观察 FN2地图 MENU+START保存" :
               "方向移动 A动作 B菜单 X拾取 Y等待 SELECT背包 START切页 MENU动作";
    }
    if( state.context == "OVERMAP" ) {
        return second_line ? "START" + command_label( command( "TOGGLE_FAST_SCROLL" ) ) +
               " L2上层 R2下层 SELECT笔记 MENU更多" : "方向移动光标 A选择 B返回 X搜索 Y回角色 L/R缩放";
    }
    const auto bindings = controls();
    const char *names[] = {"B", "A", "Y", "X", "L", "R", "SELECT", "START", "MENU", "FN1", "FN2", "L2", "R2"};
    std::string hint = second_line ? "" : "方向选择 ";
    const std::vector<unsigned> indexes = second_line ? std::vector<unsigned>{6,7,9,10,11,12} :
                                          std::vector<unsigned>{1,0,3,2,4,5};
    for( unsigned index : indexes ) {
        const command_t *entry = command( bindings[index] );
        if( entry ) { hint += std::string( names[index] ) + entry->label + " "; }
        else if( index == 0 || index == 1 ) { hint += std::string( names[index] ) + ( index ? "确认 " : "返回 " ); }
    }
    if( second_line ) { hint += bindings[7].empty() ? "START/MENU更多" : "MENU更多"; }
    return hint;
}
std::string control_hint( bool second_line )
{
    std::string hint = control_hint_core( second_line );
    if( second_line && text_cells( hint + " 电源休眠" ) <= 84 ) { hint += " 电源休眠"; }
    return hint;
}
int text_cells( const std::string &text )
{
    int result = 0;
    for( size_t i = 0; i < text.size(); ) { result += cells( decode( text, i ) ); }
    return result;
}
void render_overlay( SDL_Renderer *renderer )
{
    if( !state.enabled ) {
        return;
    }
    if( !state.font ) {
        state.font = TTF_OpenFont( "data/font/unifont.ttf", 28 );
        state.ascii_font = TTF_OpenFont( "data/font/Terminus.ttf", 28 );
        state.hud_font = TTF_OpenFont( "data/font/unifont.ttf", 24 );
        state.hud_ascii_font = TTF_OpenFont( "data/font/Terminus.ttf", 24 );
    }
    if( !state.font || !state.ascii_font || !state.hud_font || !state.hud_ascii_font ) {
        std::fprintf( stderr, "Brick fixed-width font: %s\n", TTF_GetError() );
        return;
    }
    int width, height;
    SDL_GetRendererOutputSize( renderer, &width, &height );
    Uint8 r, g, b, a;
    SDL_BlendMode blend;
    SDL_GetRenderDrawColor( renderer, &r, &g, &b, &a );
    SDL_GetRenderDrawBlendMode( renderer, &blend );
    hud( renderer, width, height );
    if( !state.keyboard && !state.help && !state.actions ) {
        SDL_SetRenderDrawBlendMode( renderer, blend );
        SDL_SetRenderDrawColor( renderer, r, g, b, a );
        return;
    }
    height -= reserved_height();
    const int panel_height = state.actions ? 560 : state.keyboard ? 464 : 420;
    SDL_Rect panel = { 24, std::max( 0, ( height - panel_height ) / 2 ), width - 48, panel_height };
    SDL_SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );
    SDL_SetRenderDrawColor( renderer, 12, 18, 28, 245 );
    SDL_RenderFillRect( renderer, &panel );
    const int x = panel.x + 16;
    const int y = panel.y + 14;
    if( state.actions ) {
        label( renderer, std::string( "动作面板 · " ) + ( gameplay() ? action_pages[state.action_page] : state.pages[state.action_page].title ) +
               " " + std::to_string( state.action_page + 1 ) + "/" + std::to_string( panel_page_count() ), x, y );
        const int cx = panel.x + panel.w / 2;
        const int cy = panel.y + 260;
        const int offsets[8][2] = { { 0, -170 }, { 220, -120 }, { 320, 0 }, { 220, 120 },
                                   { 0, 170 }, { -220, 120 }, { -320, 0 }, { -220, -120 } };
        for( int i = 0; i < 8; ++i ) {
            const auto &entries = state.pages[gameplay() ? 0 : state.action_page].entries;
            const auto *entry = i < static_cast<int>( entries.size() ) ? entries[i] : nullptr;
            if( !gameplay() && !entry ) { continue; }
            SDL_Rect cell = { cx + offsets[i][0] - 120, cy + offsets[i][1] - 24, 240, 48 };
            SDL_SetRenderDrawColor( renderer, i == state.action_selected ? 32 : 35,
                                    i == state.action_selected ? 107 : 46, i == state.action_selected ? 160 : 62, 255 );
            SDL_RenderFillRect( renderer, &cell );
            const std::string name = gameplay() ? action_labels[state.action_page][i] : command_label( entry );
            label( renderer, name, cell.x + 12, cell.y + 12 );
        }
    } else if( state.keyboard ) {
        label( renderer, state.caps ? "屏幕键盘 · 大写" : "屏幕键盘 · 小写", x, y );
        label( renderer, "键入：" + state.typed.substr( state.typed.size() > 55 ? state.typed.size() - 55 : 0 ), x, y + 34 );
        const int cell_width = ( panel.w - 32 ) / 10;
        for( int i = 0; i < 60; ++i ) {
            SDL_Rect cell = { x + ( i % 10 ) * cell_width, y + 80 + ( i / 10 ) * 48,
                             cell_width - 4, 44 };
            SDL_SetRenderDrawColor( renderer, i == state.selected ? 32 : 35,
                                    i == state.selected ? 107 : 46, i == state.selected ? 160 : 62, 255 );
            SDL_RenderFillRect( renderer, &cell );
            std::string text;
            if( i < 50 ) {
                text.assign( 1, selected_character( i ) );
            } else {
                text = special_labels[i - 50];
            }
            label( renderer, text, cell.x + 7, cell.y + 8 );
        }
    } else {
        const char *const help[] = {
            "CDDA · Brick 按键", "方向键：移动 / 选择（菜单长按快速滚动）",
            "A：确认 / 动作菜单    B：返回", "X：拾取    Y：等待一回合",
            "SELECT：背包    START：切换侧栏", "L：射击    R：装填    FN1：观察    FN2：地图",
            "点按 MENU：动作面板    MENU + SELECT：键盘", "MENU + START：保存退出",
            "MENU + A / B：是 / 否    X：检查    Y：睡眠",
            "MENU + L / R：砸毁 / 丢弃    FN1：制作    FN2：人物",
            "MENU + 上 / 下：缩放    左 / 右：上楼 / 下楼",
            "菜单按键见底栏；点按MENU查看当前菜单更多动作",
        };
        const char *const menu_help[] = {
            "CDDA · Brick 菜单按键", "方向键：选择；长按快速滚动；A确认 B返回",
            "物品栏：X搜索 Y收藏；拾取/丢弃时Y勾选",
            "物品栏：SELECT分类；更多动作按用途分组",
            "物品栏：L/R换栏 L2/R2翻页 START更多",
            "制作：L/R分类 X搜索 Y批量 SELECT收藏",
            "制作：L2/R2滚动右侧材料 START更多",
            "地图：START光标快移；人物不随光标移动",
            "地图：L/R缩放 L2/R2楼层；更多见地图组",
            "MENU+SELECT键盘；搜索与数量输入自动弹出",
            "MENU+上/下翻页 左/右首末；X清除筛选",
            "A / B / MENU 收起帮助",
        };
        for( int i = 0; i < 12; ++i ) {
            label( renderer, gameplay() ? help[i] : menu_help[i], x, y + i * 32 );
        }
    }
    SDL_SetRenderDrawBlendMode( renderer, blend );
    SDL_SetRenderDrawColor( renderer, r, g, b, a );
}
} // namespace brick_input
#endif
