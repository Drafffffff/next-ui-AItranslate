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
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#ifdef HAS_CHEEVOS
#include "ma_netplay_compat.h"
static char numbers[256][4];
static char *number_values[257];
static int octets[4] = {192, 168, 1, 1};
static char peer_address[32], address_desc[80], host_desc[80];
static int via_relay;
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
    fprintf(f, "%d %d %d %d %d %s\n", via_relay, octets[0], octets[1], octets[2], octets[3], room_code);
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
static int start_link(MenuList *list, int index) {
    if (Netplay_busy()) {
        Menu_message("Disconnect the current link first.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    if (Netplay_used()) {
        Menu_message("Exit and reopen the game before\nstarting a new link session.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    if (!candidate_game()) {
        Menu_message("No supported link mode for this ROM.\nSee the GBA Link compatibility list.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    if (!identify_rom() || !backup_sram() || !State_prepareLink()) {
        Menu_message("Cannot prepare ROM/save/resume guard.\nLink was not started.", (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
    bool ok = via_relay ? Netplay_relay(peer_address, 55435, room_code, index == 0) :
              (index == 0 ? Netplay_host(55435) : Netplay_join(peer_address, 55435));
    if (!ok) {
        Menu_message((char *)Netplay_status(), (char*[]){"B", "BACK", NULL});
        return MENU_CALLBACK_NOP;
    }
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
    list->items[0].desc = via_relay ? address_desc : host_desc;
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
int OptionNetplay_openMenu(MenuList *list, int index) {
    load_connection();
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
    MenuItem items[] = {
        {.name="Host Link", .desc=via_relay ? address_desc : host_desc, .on_confirm=start_link},
        {.name="Join Link", .desc=address_desc, .on_confirm=start_link},
        {.name="Host / Relay Address", .desc=address_desc, .on_confirm=address_menu},
        {.name="Connection", .values=connection_values, .value=via_relay, .on_change=connection_change},
        {.name="Room Code", .desc=room_code, .on_confirm=room_menu},
        {.name="Disconnect", .desc=(char *)Netplay_status(), .on_confirm=end_link},
        {NULL}
    };
    MenuList menu = {.type=MENU_LIST, .items=items};
    Menu_options(&menu);
    return show_menu ? MENU_CALLBACK_NOP : MENU_CALLBACK_EXIT;
}

#else
int OptionNetplay_openMenu(MenuList *list, int index) {
    Menu_message("GBA Link is available in the tg5040 build.", (char*[]){"B", "BACK", NULL});
    return MENU_CALLBACK_NOP;
}
#endif
