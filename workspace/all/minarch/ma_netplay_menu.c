#include "ma_internal.h"
#include "ma_netplay.h"
#include "ma_frontend_opts.h"
#include "ma_options.h"
#include "ma_ai.h"
#include "ma_saves.h"
#ifdef HAS_CHEEVOS
#include <openssl/evp.h>
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#ifdef HAS_CHEEVOS
#include "ma_netplay_compat.h"
static char numbers[256][4];
static char *number_values[257];
static int octets[4] = {192, 168, 1, 1};
static char peer_address[32], address_desc[80], host_desc[80];
static int via_relay, connection_mode;
#ifdef NEXTUI_LINK_BT
/* Keep persisted values: Wi-Fi=0, Bluetooth=1, Hotspot=2. */
static char *mode_tabs[] = {"Bluetooth", "Wi-Fi", "Hotspot", NULL};
static const int tab_modes[] = {1, 0, 2};
#endif
static char *connection_values[] = {"Direct Wi-Fi", "Public Relay", NULL};
static int room_digits[6] = {1, 2, 3, 4, 5, 6};
static char room_code[7] = "123456";
static char *digit_values[11];
static bool config_loaded;
static void save_connection(void) {
    char path[] = SHARED_USERDATA_PATH "/gba-link.conf";
    char temporary[] = SHARED_USERDATA_PATH "/gba-link.conf.tmp";
    FILE *f = fopen(temporary, "w");
    if (!f) return;
    fprintf(f, "%d %d %d %d %d %s %d\n", via_relay, octets[0], octets[1], octets[2], octets[3], room_code, connection_mode);
    bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f)) ok = false;
    if (ok) rename(temporary, path); else unlink(temporary);
}
static void load_connection(void) {
    if (config_loaded) return;
    config_loaded = true;
    FILE *f = fopen(SHARED_USERDATA_PATH "/gba-link.conf", "r");
    if (!f) return;
    int relay, a, b, c, d;
    char room[7];
    if (fscanf(f, "%d %d %d %d %d %6s", &relay, &a, &b, &c, &d, room) == 6 &&
        relay >= 0 && relay <= 1 && a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
        c >= 0 && c <= 255 && d >= 0 && d <= 255 && strlen(room) == 6 &&
        strspn(room, "0123456789") == 6) {
        via_relay = relay; octets[0] = a; octets[1] = b; octets[2] = c; octets[3] = d;
        memcpy(room_code, room, 7);
        for (int i = 0; i < 6; i++) room_digits[i] = room[i] - '0';
    }
    int mode = 0;
    if (fscanf(f, "%d", &mode) == 1 && mode >= 0 && mode <= 2) connection_mode = mode;
    fclose(f);
}

static unsigned char rom_hash[32];
static bool hash_file(const char *path, unsigned char hash[32]) {
    FILE *f = fopen(path, "rb");
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char buf[16384];
    unsigned hash_size = 0;
    bool ok = f && ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1;
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), f))) ok = EVP_DigestUpdate(ctx, buf, n) == 1;
    if (f && ferror(f)) ok = false;
    if (ok) ok = EVP_DigestFinal_ex(ctx, hash, &hash_size) == 1 && hash_size == 32;
    if (f) fclose(f);
    EVP_MD_CTX_free(ctx);
    return ok;
}
static bool identify_rom(void) {
    const char *path = game.tmp_path[0] ? game.tmp_path : game.path;
    unsigned char core_hash[32];
    if (!hash_file(path, rom_hash) || !hash_file(core.image_path, core_hash)) return false;
    Netplay_identity(rom_hash, core_hash, OptionList_getOptionValue(&config.core, "gpsp_serial"));
    return true;
}
/* Snapshot in-memory SRAM before starting a session. Never replace a player's
 * existing save with another player's save. Unique backup, fsynced before use. */
static bool backup_sram(void) {
    size_t size = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    const void *data = core.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!size) return true;
    if (!data) return false;
    char path[MAX_PATH];
    int n = snprintf(path, sizeof(path), "%s/.gba-link-%02x%02x%02x%02x-before-%ld-XXXXXX", core.saves_dir,
                     rom_hash[0], rom_hash[1], rom_hash[2], rom_hash[3], (long)time(NULL));
    if (n < 0 || n >= (int)sizeof(path)) return false;
    int fd = mkstemp(path);
    if (fd < 0) return false;
    size_t offset = 0;
    while (offset < size) {
        ssize_t written = write(fd, (const char *)data + offset, size - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        offset += written;
    }
    bool ok = offset == size && fsync(fd) == 0;
    if (close(fd)) ok = false;
    if (!ok) unlink(path);
    return ok;
}
static bool candidate_game(void) {
    const char *mode = OptionList_getOptionValue(&config.core, "gpsp_serial");
    if (mode && !strcmp(mode, "disabled")) return false;
    const char *path = game.tmp_path[0] ? game.tmp_path : game.path;
    FILE *file = fopen(path, "rb"); char code[5] = {0};
    if (!file) return false;
    bool ok = fseek(file, 0xac, SEEK_SET) == 0 && fread(code, 1, 4, file) == 4;
    fclose(file);
    if (!ok) return false;
    if (!strcmp(code, "NLNK") && mode && !strcmp(mode, "mul_poke")) return true;
    for (int i = 0; netplay_candidate_codes[i]; i++)
        if (!strcmp(code, netplay_candidate_codes[i])) return true;
    return false;
}
/* Native, cancellable progress panel. Game callbacks and menus stay on the
 * frontend thread; discovery and radio I/O remain in the private helper. */
static void link_progress_draw(const char *text) {
    GFX_clear(screen);
    GFX_blitMessage(font.medium, (char *)text, screen, &(SDL_Rect){SCALE1(PADDING), SCALE1(PADDING),
        screen->w-SCALE1(2*PADDING), screen->h-SCALE1(PILL_SIZE+PADDING)});
    GFX_blitButtonGroup((char*[]){"B", "CANCEL", NULL}, 0, screen, 1);
    GFX_flip(screen);
    hdmimon();
}
/* Read only the local interface address; the full Wi-Fi status API launches
 * external commands and must not run in this per-frame waiting loop. */
static void link_local_ip(char ip[INET_ADDRSTRLEN]) {
    ip[0] = 0;
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces)) return;
    for (struct ifaddrs *i = interfaces; i; i = i->ifa_next) {
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !strcmp(i->ifa_name, "wlan0")) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, ip, INET_ADDRSTRLEN);
            break;
        }
    }
    freeifaddrs(interfaces);
}
static bool link_wait(bool host) {
    GFX_setMode(MODE_MAIN);
    bool connected = false;
    char local_ip[INET_ADDRSTRLEN] = {0};
    uint32_t ip_updated = SDL_GetTicks() - 1000;
    while (1) {
        GFX_startFrame(); PAD_poll();
        if (PAD_justPressed(BTN_B)) { Netplay_stop("Connection cancelled"); break; }
        Netplay_poll();
        if (Netplay_active()) { connected = true; break; }
        if (!Netplay_busy()) break;
        int dirty = 1;
        PWR_update(&dirty, NULL, Menu_beforeSleep, Menu_afterSleep);
        char waiting[256];
        const char *text = Netplay_status();
        if (host && connection_mode == 0 && !via_relay && Netplay_state() == NP_LISTENING) {
            uint32_t now = SDL_GetTicks();
            if ((uint32_t)(now - ip_updated) >= 1000) {
                link_local_ip(local_ip);
                ip_updated = now;
            }
            snprintf(waiting, sizeof(waiting), "Room created\n%s : 55435\n\nWaiting for another Brick...",
                local_ip[0] ? local_ip : "Wi-Fi address unavailable");
            text = waiting;
        }
        link_progress_draw(text);
    }
    GFX_setMode(MODE_MENU);
    if (!connected && Netplay_state() == NP_ERROR)
        Menu_message((char *)Netplay_status(), (char*[]){"B", "BACK", NULL});
    return connected;
}
#ifdef NEXTUI_LINK_BT
static int selected_room;
static int select_room(MenuList *list, int index) {
    selected_room = index;
    return MENU_CALLBACK_EXIT;
}
static bool nearby_find(char peer[18]) {
    if (!(connection_mode==2?Netplay_hotspotScan():Netplay_bluetoothScan())) {
        Menu_message("Cannot search for rooms.\nCheck the selected radio and try again.", (char*[]){"B", "BACK", NULL});
        return false;
    }
    struct NetplayBTRoom rooms[NETPLAY_BT_ROOMS];
    unsigned count = 0;
    bool cancelled = false;
    char failure[160] = {0};
    uint32_t began = SDL_GetTicks();
    GFX_setMode(MODE_MAIN);
    while (1) {
        GFX_startFrame(); PAD_poll();
        if (PAD_justPressed(BTN_B)) { cancelled = true; break; }
        Netplay_poll();
        count = Netplay_btRooms(rooms);
        if (Netplay_btScanFailed()) { count = 0; snprintf(failure,sizeof(failure),"%s",Netplay_btScanStatus()); break; }
        if (count && SDL_GetTicks()-began >= 2000) break;
        if (SDL_GetTicks()-began >= 12000) break;
        int dirty = 1;
        PWR_update(&dirty, NULL, Menu_beforeSleep, Menu_afterSleep);
        link_progress_draw("Searching nearby rooms...");
    }
    Netplay_bluetoothScanStop();
    GFX_setMode(MODE_MENU);
    if (cancelled) return false;
    if (!count) {
        Menu_message(failure[0] ? failure : "No nearby rooms.\nCreate a room on the other console first.", (char*[]){"B", "BACK", NULL});
        return false;
    }
    selected_room = count == 1 ? 0 : -1;
    if (count > 1) {
        MenuItem items[NETPLAY_BT_ROOMS+1] = {0};
        for (unsigned i = 0; i < count; i++)
            items[i] = (MenuItem){.name=rooms[i].label, .on_confirm=select_room};
        MenuList menu = {.type=MENU_LIST, .items=items};
        Menu_options(&menu);
    }
    if (selected_room < 0) return false;
    memcpy(peer, rooms[selected_room].address, 18);
    return true;
}
#endif
static int start_link(MenuList *list, int index) {
    if (Netplay_busy()) {
        Menu_message("Disconnect the current link first.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    if (Netplay_used()) {
        Menu_message("Exit and reopen the game before\nstarting a new link session.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    bool host = list->items[index].id == 0;
    if (!candidate_game()) {
        Menu_message("No supported link mode for this ROM.\nSee the GBA Link compatibility list.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
#ifdef NEXTUI_LINK_BT
    /* Choosing a Bluetooth link explicitly enables the shared system setting.
     * Hardware startup is cancellable in the private worker, never on this UI
     * thread. Brick Mic cleanup then preserves Bluetooth for the next app. */
    if (connection_mode == 1 && !CFG_getBluetooth()) {
        CFG_setBluetooth(true);
    }
    char bluetooth_peer[18] = {0};
    if (connection_mode != 0 && !host && !nearby_find(bluetooth_peer)) return MENU_CALLBACK_NOP;
#endif
    if (!identify_rom() || !backup_sram() || !State_prepareLink()) {
        Menu_message("Cannot prepare ROM/save/resume guard.\nLink was not started.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    bool ok;
#ifdef NEXTUI_LINK_BT
    if (connection_mode == 2) ok = Netplay_hotspot(host, bluetooth_peer);
    else if (connection_mode == 1) ok = Netplay_bluetooth(host, bluetooth_peer);
    else
#endif
    ok = via_relay ? Netplay_relay(peer_address, 55435, room_code, host) :
              (host ? Netplay_host(55435) : Netplay_join(peer_address, 55435));
    if (!ok) {
        Menu_message((char *)Netplay_status(), (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    if (!link_wait(host)) return MENU_CALLBACK_NOP;
    AI_playStopFor("GBA Link started");
    fast_forward = ff_toggled = ff_hold_active = ff_paused_by_rewind_hold = 0;
    rewind_toggle = rewind_pressed = rewinding = 0;
    show_menu = 0;
    return MENU_CALLBACK_EXIT;
}
static int end_link(MenuList *list, int index) {
    Netplay_stop("Disconnected by player");
    return MENU_CALLBACK_EXIT;
}
static int address_change(MenuList *list, int index) {
    octets[index] = list->items[index].value;
    snprintf(peer_address, sizeof(peer_address), "%d.%d.%d.%d", octets[0], octets[1], octets[2], octets[3]);
    save_connection();
    return MENU_CALLBACK_NOP;
}
static int address_menu(MenuList *list, int index) {
    MenuItem items[5] = {0};
    char *labels[] = {"Address 1", "Address 2", "Address 3", "Address 4"};
    for (int i = 0; i < 4; i++) items[i] = (MenuItem){.name=labels[i], .values=number_values, .value=octets[i]};
    MenuList address = {.type=MENU_VAR, .items=items, .on_change=address_change};
    Menu_options(&address);
    snprintf(address_desc, sizeof(address_desc), "%s : 55435", peer_address);
    return MENU_CALLBACK_NOP;
}
static int connection_change(MenuList *list, int index) {
    via_relay = list->items[index].value;
    save_connection();
    return MENU_CALLBACK_NOP;
}
static int room_change(MenuList *list, int index) {
    room_digits[index] = list->items[index].value;
    for (int i = 0; i < 6; i++) room_code[i] = '0' + room_digits[i];
    save_connection();
    return MENU_CALLBACK_NOP;
}
static int room_menu(MenuList *list, int index) {
    MenuItem items[7] = {0};
    char *labels[] = {"Room 1", "Room 2", "Room 3", "Room 4", "Room 5", "Room 6"};
    for (int i = 0; i < 6; i++) items[i] = (MenuItem){.name=labels[i], .values=digit_values, .value=room_digits[i]};
    MenuList room = {.type=MENU_VAR, .items=items, .on_change=room_change};
    return Menu_options(&room);
}
static int advanced_menu(MenuList *list, int index) {
    if (connection_mode) return MENU_CALLBACK_NOP;
    MenuItem items[] = {
        {.name="Connection", .values=connection_values, .value=via_relay, .on_change=connection_change},
        {.name="Host / Relay Address", .desc=address_desc, .on_confirm=address_menu},
        {.name="Room Code", .desc=room_code, .on_confirm=room_menu},
        {NULL}
    };
    MenuList menu = {.type=MENU_LIST, .items=items};
    return Menu_options(&menu);
}
#ifdef NEXTUI_LINK_BT
static bool reopen_link_menu;
static int mode_select(MenuList *list, int index) {
    if (Netplay_busy()) {
        Menu_message("Disconnect the current link first.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    if (index < 0 || index >= 3) return MENU_CALLBACK_NOP;
    connection_mode = tab_modes[index];
    save_connection();
    reopen_link_menu = true;
    return MENU_CALLBACK_EXIT;
}
#endif
int OptionNetplay_openMenu(MenuList *list, int index) {
    load_connection();
#ifndef NEXTUI_LINK_BT
    connection_mode = 0;
#endif
    if (!Netplay_supported() || strcmp(core.name, "gpsp")) {
        Menu_message("GBA Link requires the updated gpSP core.\nSee the GBA Link installation guide.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    for (int i = 0; i < 256; i++) { snprintf(numbers[i], 4, "%d", i); number_values[i] = numbers[i]; }
    for (int i = 0; i < 10; i++) digit_values[i] = number_values[i];
    snprintf(peer_address, sizeof(peer_address), "%d.%d.%d.%d", octets[0], octets[1], octets[2], octets[3]);
    snprintf(address_desc, sizeof(address_desc), "%s : 55435", peer_address);
    struct WIFI_connection wifi = {0};
    WIFI_connectionInfo(&wifi);
    snprintf(host_desc, sizeof(host_desc), "%s : 55435", wifi.ip[0] ? wifi.ip : "Connect Wi-Fi first");
#ifdef NEXTUI_LINK_BT
    do {
    reopen_link_menu = false;
#endif
    MenuItem items[5] = {
        {.name="Create Room", .id=0, .desc=connection_mode ? "Wait for the other console" : host_desc, .on_confirm=start_link},
        {.name="Join Room", .id=1, .desc=connection_mode ? "Find nearby rooms" : address_desc, .on_confirm=start_link}
    };
    int count = 2;
    if (Netplay_busy()) items[count++] = (MenuItem){.name="Disconnect", .desc=(char *)Netplay_status(), .on_confirm=end_link};
    if (connection_mode == 0) items[count++] = (MenuItem){.name="Advanced", .on_confirm=advanced_menu};
    MenuList menu = {.type=MENU_LIST, .items=items};
#ifdef NEXTUI_LINK_BT
    menu.tabs = mode_tabs;
    menu.tab = connection_mode == 1 ? 0 : connection_mode == 0 ? 1 : 2;
    menu.on_tab_change = mode_select;
#endif
    Menu_options(&menu);
#ifdef NEXTUI_LINK_BT
    } while (reopen_link_menu && show_menu);
#endif
    return show_menu ? MENU_CALLBACK_NOP : MENU_CALLBACK_EXIT;
}

#else
int OptionNetplay_openMenu(MenuList *list, int index) {
    Menu_message("GBA Link is available in the tg5040 build.", (char*[]){"B", "BACK", NULL});
    return MENU_CALLBACK_NOP;
}
#endif
