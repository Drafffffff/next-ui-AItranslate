/* Stage-one native PocketJS core probe. No TypeScript/QuickJS guest yet. */
#include <SDL.h>
#include <SDL_ttf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include "pocket_ui_cabi.h"
#include "pocket_props.h"

#define WIDTH 1024
#define HEIGHT 768
#define CARDS 3
static volatile sig_atomic_t stop_requested;
static int cards[CARDS], selected, activated, quit;
static unsigned frames_presented;
static double raster_ms, max_raster_ms;

static void request_stop(int sig) { (void)sig; stop_requested = 1; }
static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}
static uint32_t abgr(unsigned r, unsigned g, unsigned b) {
    return 0xff000000U | (b << 16) | (g << 8) | r;
}
static int panel(int x, int y, int w, int h, uint32_t color, int radius) {
    int id = ui_create_node(0);
    ui_insert_before(1, id, 0);
    ui_set_prop(id, PROP_POS_TYPE, 1);
    ui_set_prop(id, PROP_INSET_L, x);
    ui_set_prop(id, PROP_INSET_T, y);
    ui_set_prop(id, PROP_WIDTH, w);
    ui_set_prop(id, PROP_HEIGHT, h);
    ui_set_prop(id, PROP_RADIUS, radius);
    ui_set_prop(id, PROP_BG_COLOR, color);
    return id;
}
static void update_selection(void) {
    for (int i = 0; i < CARDS; i++) {
        uint32_t color = i == selected ? abgr(27, 103, 72) : abgr(30, 39, 51);
        if (activated && i == selected) color = abgr(47, 158, 103);
        ui_set_prop(cards[i], PROP_BG_COLOR, color);
    }
}
static void scene_init(void) {
    ui_init(1);
    ui_set_viewport(WIDTH, HEIGHT);
    ui_set_prop(1, PROP_BG_COLOR, abgr(15, 19, 23));
    panel(40, 32, 944, 144, abgr(25, 33, 43), 20);
    for (int i = 0; i < CARDS; i++) cards[i] = panel(40, 216 + i * 136, 944, 120, 0, 16);
    const uint32_t colors[] = {abgr(255,0,0), abgr(0,255,0), abgr(0,0,255), abgr(255,255,255)};
    for (int i = 0; i < 4; i++) panel(40 + 236 * i, 696, 236, 24, colors[i], 0);
    selected = activated = quit = 0;
    update_selection();
}
static void move_selection(int delta) {
    selected = (selected + delta + CARDS) % CARDS;
    activated = 0;
    update_selection();
}
static void handle_event(const SDL_Event *event) {
    if (event->type == SDL_QUIT) quit = 1;
    else if (event->type == SDL_KEYDOWN && !event->key.repeat) {
        switch (event->key.keysym.sym) {
            case SDLK_UP: case SDLK_LEFT: move_selection(-1); break;
            case SDLK_DOWN: case SDLK_RIGHT: move_selection(1); break;
            case SDLK_RETURN: case SDLK_SPACE: activated = !activated; update_selection(); break;
            case SDLK_ESCAPE: case SDLK_BACKSPACE: quit = 1; break;
            default: break;
        }
    } else if (event->type == SDL_JOYHATMOTION) {
        if (event->jhat.value & (SDL_HAT_UP | SDL_HAT_LEFT)) move_selection(-1);
        else if (event->jhat.value & (SDL_HAT_DOWN | SDL_HAT_RIGHT)) move_selection(1);
    } else if (event->type == SDL_JOYBUTTONDOWN) {
        /* Same tg5040 mapping as NextUI: A=1, B=0, MENU=8. */
        if (event->jbutton.button == 1) { activated = !activated; update_selection(); }
        else if (event->jbutton.button == 0 || event->jbutton.button == 8) quit = 1;
    }
}
static const uint8_t *render(void) {
    double start = now_ms();
    ui_tick();
    const uint8_t *pixels = ui_render_incremental();
    double elapsed = now_ms() - start;
    raster_ms += elapsed;
    if (elapsed > max_raster_ms) max_raster_ms = elapsed;
    if (!pixels || ui_framebuffer_width() != WIDTH || ui_framebuffer_height() != HEIGHT ||
        ui_framebuffer_stride() != WIDTH * 4 || ui_framebuffer_len() != WIDTH * HEIGHT * 4) {
        fprintf(stderr, "Invalid PocketJS framebuffer\n");
        return NULL;
    }
    return pixels;
}
static uint32_t pixel(const uint8_t *pixels, unsigned x, unsigned y) {
    uint32_t value;
    memcpy(&value, pixels + ((size_t)y * WIDTH + x) * 4, sizeof(value));
    return value;
}
static int save_bmp(const uint8_t *pixels, const char *path) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom((void *)pixels, WIDTH, HEIGHT,
        32, WIDTH * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return -1;
    int result = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    return result;
}
static int self_test(const char *dump) {
    int ok = 0;
    scene_init();
    const uint8_t *pixels = render();
    if (!pixels || pixel(pixels, 5, 5) != 0xff0f1317U ||
        pixel(pixels, 100, 705) != 0xffff0000U ||
        pixel(pixels, 300, 705) != 0xff00ff00U ||
        pixel(pixels, 600, 705) != 0xff0000ffU) goto done;
    if (dump && save_bmp(pixels, dump) != 0) goto done;
    uint32_t active_pixel = pixel(pixels, 80, 250);
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_JOYHATMOTION; event.jhat.value = SDL_HAT_DOWN;
    handle_event(&event);
    pixels = render();
    if (!pixels || selected != 1 || pixel(pixels, 80, 250) == active_pixel) goto done;
    event.type = SDL_JOYBUTTONDOWN; event.jbutton.button = 1;
    handle_event(&event);
    pixels = render();
    if (!pixels || !activated || pixel(pixels, 80, 390) != 0xff2f9e67U) goto done;
    event.jbutton.button = 8; handle_event(&event);
    if (!quit) goto done;
    for (int i = 0; i < 120; i++) {
        move_selection(1);
        if (!render()) goto done;
    }
    ok = 1;
done:
    ui_shutdown();
    /* Verify lifecycle can restart in the same process, with fresh state. */
    if (ok) {
        scene_init();
        pixels = render();
        ok = pixels && selected == 0 && !activated && !quit;
        ui_shutdown();
    }
    printf("%s: core framebuffer, RGB order, incremental update, input, shutdown/restart\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
static SDL_Texture *label(SDL_Renderer *renderer, TTF_Font *font, const char *text) {
    if (!font) return NULL;
    SDL_Color white = {236, 243, 247, 255};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, white);
    if (!surface) return NULL;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}
static void draw_label(SDL_Renderer *renderer, SDL_Texture *texture, int x, int y) {
    if (!texture) return;
    SDL_Rect rect = {x, y, 0, 0};
    SDL_QueryTexture(texture, NULL, NULL, &rect.w, &rect.h);
    SDL_RenderCopy(renderer, texture, NULL, &rect);
}
int main(int argc, char **argv) {
    int test = 0, dummy = 0, frame_limit = 0, status = 1;
    const char *dump = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--self-test")) test = 1;
        else if (!strcmp(argv[i], "--dummy-video")) dummy = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end; long n = strtol(argv[++i], &end, 10);
            if (*end || n < 1 || n > 1000000) return 2;
            frame_limit = (int)n;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else { fprintf(stderr, "Usage: %s [--self-test] [--dummy-video] [--frames N] [--dump image.bmp]\n", argv[0]); return 2; }
    }
    if (test) return self_test(dump);
    if (dummy && setenv("SDL_VIDEODRIVER", "dummy", 1) != 0) return 1;
    if (dummy && !frame_limit) frame_limit = 3;
    signal(SIGINT, request_stop); signal(SIGTERM, request_stop);
    double boot = now_ms();
    printf("PocketJS Brick native-core smoke\nupstream=%s\n", POCKETJS_REV);
    printf("TypeScript/QuickJS guest: not connected in this stage\n");
    printf("SDL video drivers:");
    for (int i = 0; i < SDL_GetNumVideoDrivers(); i++) printf(" %s", SDL_GetVideoDriver(i));
    printf("\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *window = SDL_CreateWindow("PocketJS Brick Smoke", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!renderer && window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) { fprintf(stderr, "SDL display: %s\n", SDL_GetError()); goto sdl_done; }
    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    if (!texture) { fprintf(stderr, "SDL texture: %s\n", SDL_GetError()); goto renderer_done; }
    int joystick_count = SDL_NumJoysticks();
    SDL_Joystick **joysticks = calloc((size_t)joystick_count, sizeof(*joysticks));
    if (joystick_count && !joysticks) goto texture_done;
    for (int i = 0; i < joystick_count; i++) joysticks[i] = SDL_JoystickOpen(i);
    int ttf_ok = TTF_Init() == 0;
    const char *font_path = getenv("POCKETJS_FONT");
    if (!font_path) font_path = "/mnt/SDCARD/.system/res/font1.ttf";
    TTF_Font *font = ttf_ok ? TTF_OpenFont(font_path, 32) : NULL;
    if (!font) fprintf(stderr, "Labels unavailable: %s\n", TTF_GetError());
    const char *texts[] = {"PocketJS / NextUI / Brick", "Native core + SDL2 / stage 1",
        "01  Native layout", "02  Input / press A", "03  Software rasterizer",
        "D-pad: select    A: toggle    B / MENU: exit"};
    SDL_Texture *labels[6];
    for (int i = 0; i < 6; i++) labels[i] = label(renderer, font, texts[i]);
    scene_init();
    while (!quit && !stop_requested && (!frame_limit || frames_presented < (unsigned)frame_limit)) {
        double start = now_ms();
        SDL_Event event;
        while (SDL_PollEvent(&event)) handle_event(&event);
        if (quit || stop_requested) break;
        const uint8_t *pixels = render();
        if (!pixels || SDL_UpdateTexture(texture, NULL, pixels, WIDTH * 4) != 0) goto scene_done;
        if (SDL_RenderClear(renderer) != 0 || SDL_RenderCopy(renderer, texture, NULL, NULL) != 0) goto scene_done;
        draw_label(renderer, labels[0], 72, 52); draw_label(renderer, labels[1], 72, 110);
        for (int i = 0; i < 3; i++) draw_label(renderer, labels[2 + i], 72, 252 + i * 136);
        draw_label(renderer, labels[5], 42, 635);
        SDL_RenderPresent(renderer);
        frames_presented++;
        if (frames_presented == 1) printf("first_present_ms=%.3f video=%s\n", now_ms() - boot, SDL_GetCurrentVideoDriver());
        if (dump && frames_presented == 1 && save_bmp(pixels, dump) != 0) goto scene_done;
        double delay = 1000.0 / 30.0 - (now_ms() - start);
        if (delay > 0) SDL_Delay((Uint32)delay);
    }
    status = 0;
scene_done:
    if (status) fprintf(stderr, "SDL presentation failed: %s\n", SDL_GetError());
    ui_shutdown();
    for (int i = 0; i < 6; i++) SDL_DestroyTexture(labels[i]);
    if (font) TTF_CloseFont(font);
    if (ttf_ok) TTF_Quit();
    for (int i = 0; i < joystick_count; i++) if (joysticks[i]) SDL_JoystickClose(joysticks[i]);
    free(joysticks);
texture_done:
    SDL_DestroyTexture(texture);
renderer_done:
    SDL_DestroyRenderer(renderer);
sdl_done:
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    printf("frames=%u raster_total_ms=%.3f raster_max_ms=%.3f peak_rss_kib=%ld exit=%d\n",
        frames_presented, raster_ms, max_raster_ms, usage.ru_maxrss, status);
    return status;
}
