/* Experimental Brick host for PocketJS Solid/QuickJS applications. */
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include "pocket_runtime.h"
#include "pocket_spec.h"
#define WIDTH 1024
#define HEIGHT 768
static volatile sig_atomic_t stopped;
static uint32_t buttons;
static double max_work_ms;
static void stop(int sig) { (void)sig; stopped = 1; }
static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}
static void *read_file(const char *path, size_t *length) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size <= 0 || size > 32 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    char *data = malloc((size_t)size + 1);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); return NULL; }
    fclose(f); data[size] = 0; *length = (size_t)size; return data;
}
static uint32_t key(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: return POCKET_BTN_UP; case SDLK_RIGHT: return POCKET_BTN_RIGHT;
        case SDLK_DOWN: return POCKET_BTN_DOWN; case SDLK_LEFT: return POCKET_BTN_LEFT;
        case SDLK_RETURN: case SDLK_SPACE: return POCKET_BTN_CIRCLE;
        default: return 0;
    }
}
static void event(const SDL_Event *e) {
    if (e->type == SDL_QUIT) stopped = 1;
    else if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
        if (e->type == SDL_KEYDOWN && (e->key.keysym.sym == SDLK_ESCAPE || e->key.keysym.sym == SDLK_BACKSPACE)) stopped = 1;
        uint32_t bit = key(e->key.keysym.sym);
        if (e->type == SDL_KEYDOWN) buttons |= bit; else buttons &= ~bit;
    } else if (e->type == SDL_JOYHATMOTION) {
        buttons &= ~0xf0U;
        if (e->jhat.value & SDL_HAT_UP) buttons |= POCKET_BTN_UP;
        if (e->jhat.value & SDL_HAT_RIGHT) buttons |= POCKET_BTN_RIGHT;
        if (e->jhat.value & SDL_HAT_DOWN) buttons |= POCKET_BTN_DOWN;
        if (e->jhat.value & SDL_HAT_LEFT) buttons |= POCKET_BTN_LEFT;
    } else if (e->type == SDL_JOYBUTTONDOWN || e->type == SDL_JOYBUTTONUP) {
        if (e->jbutton.button == 1) {
            if (e->type == SDL_JOYBUTTONDOWN) buttons |= POCKET_BTN_CIRCLE;
            else buttons &= ~POCKET_BTN_CIRCLE;
        } else if (e->type == SDL_JOYBUTTONDOWN && (e->jbutton.button == 0 || e->jbutton.button == 8)) stopped = 1;
    } else if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) buttons = 0;
}
static const uint8_t *step(uint32_t mask) {
    PocketRuntimeInput input = {0}; input.buttons = mask;
    double start = now_ms();
    if (!pocket_runtime_tick(&input)) return NULL;
    const uint8_t *p = pocket_runtime_render();
    double elapsed = now_ms() - start; if (elapsed > max_work_ms) max_work_ms = elapsed;
    if (!p || pocket_runtime_width() != WIDTH || pocket_runtime_height() != HEIGHT ||
        pocket_runtime_stride() != WIDTH * 4 || pocket_runtime_length() != WIDTH * HEIGHT * 4) return NULL;
    return p;
}
static int dump_ppm(const uint8_t *p, const char *path) {
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        unsigned char rgb[] = {p[4*i+2],p[4*i+1],p[4*i]};
        if (fwrite(rgb, 1, 3, f) != 3) { fclose(f); return 0; }
    }
    return fclose(f) == 0;
}
static int self_test(const char *dump) {
    const uint8_t *p = step(0); if (!p) return 1;
    uint32_t color; memcpy(&color, p + (210 * WIDTH + 50) * 4, 4);
    if (color != 0xff344d2eU) { fprintf(stderr, "Selected row is not highlighted: %08x\n", color); return 1; }
    if (dump && !dump_ppm(p, dump)) return 1;
    /* Exercise actual SDL -> portable input -> guest -> virtual-list behavior. */
    SDL_Event e; memset(&e, 0, sizeof(e)); e.type = SDL_JOYHATMOTION;
    for (int i = 0; i < 7; i++) {
        e.jhat.value = SDL_HAT_DOWN; event(&e); if (!step(buttons)) return 1;
        e.jhat.value = SDL_HAT_CENTERED; event(&e);
        for (int j = 0; j < 30; j++) if (!step(buttons)) return 1;
    }
    if (strcmp(pocket_runtime_action_name(), "list.state") || pocket_runtime_action_value() / 10000 != 7 ||
        pocket_runtime_action_value() % 10000 <= 0) { fprintf(stderr, "List navigation/scroll failed: %s %d\n", pocket_runtime_action_name(), pocket_runtime_action_value()); return 1; }
    p = step(0); if (dump && (!p || !dump_ppm(p, dump))) return 1;
    e.type = SDL_JOYBUTTONDOWN; e.jbutton.button = 1; event(&e);
    if (!step(buttons) || strcmp(pocket_runtime_action_name(), "list.activate") || pocket_runtime_action_value() != 7) return 1;
    unsigned long sequence = pocket_runtime_action_sequence();
    for (int i = 0; i < 5; i++) if (!step(buttons)) return 1;
    if (pocket_runtime_action_sequence() != sequence) return 1; /* held A is one press */
    e.type = SDL_JOYBUTTONUP; event(&e); if (!step(buttons)) return 1;
    e.type = SDL_JOYBUTTONDOWN; event(&e); if (!step(buttons) || pocket_runtime_action_sequence() != sequence + 1) return 1;
    e.type = SDL_JOYBUTTONUP; event(&e); if (!step(buttons)) return 1;
    e.type = SDL_JOYHATMOTION; e.jhat.value = SDL_HAT_DOWN; event(&e);
    for (int i = 0; i < 160; i++) if (!step(buttons)) return 1;
    if (pocket_runtime_action_value() / 10000 != 11 || pocket_runtime_action_value() % 10000 > 560) return 1;
    e.jhat.value = SDL_HAT_UP; event(&e);
    for (int i = 0; i < 180; i++) if (!step(buttons)) return 1;
    if (pocket_runtime_action_value() != 0) return 1;
    e.type = SDL_JOYBUTTONDOWN; e.jbutton.button = 8; event(&e); if (!stopped) return 1;
    stopped = 0; e.jbutton.button = 0; event(&e); if (!stopped) return 1;
    puts("PASS: QuickJS boot, Chinese atlas, guest navigation, virtual-list scrolling, visible selection, A edges, held navigation/clamped bounds, B/MENU exit");
    return 0;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    int test = 0, dummy = 0, limit = 0, result = 1;
    const char *dump = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--self-test")) test = 1;
        else if (!strcmp(argv[i], "--dummy-video")) dummy = 1;
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end; long n = strtol(argv[++i], &end, 10);
            if (*end || n < 1 || n > 1000000) return 2;
            limit = (int)n;
        } else return 2;
    }
    double boot = now_ms(); size_t js_size = 0, pack_size = 0;
    char *js = read_file("brick-app.js", &js_size);
    uint8_t *pack = read_file("brick-app.pak", &pack_size);
    if (!js || !pack) { fprintf(stderr, "Cannot read brick-app.js / brick-app.pak\n"); goto done; }
    if (!pocket_runtime_boot(js, js_size, pack, pack_size, WIDTH, HEIGHT)) goto done;
    printf("PocketJS Brick app / Solid + QuickJS\nupstream=%s guest_boot_ms=%.3f\n", POCKETJS_REV, now_ms() - boot);
    if (test) {
        result = self_test(dump);
        if (!result) {
            pocket_runtime_shutdown(); buttons = 0; stopped = 0;
            if (!pocket_runtime_boot(js, js_size, pack, pack_size, WIDTH, HEIGHT) || !step(0) ||
                strcmp(pocket_runtime_action_name(), "list.state") || pocket_runtime_action_value() != 0) result = 1;
            else puts("PASS: guest shutdown/restart resets list state");
        }
        goto done;
    }
    if (dummy && setenv("SDL_VIDEODRIVER", "dummy", 1)) goto done;
    if (dummy && !limit) limit = 4;
    signal(SIGINT, stop); signal(SIGTERM, stop);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER)) goto done;
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *window = SDL_CreateWindow("PocketJS Brick App", 0, 0, WIDTH, HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!renderer && window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture *texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT) : NULL;
    if (texture) SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);
    int njoy = SDL_NumJoysticks(); SDL_Joystick **joys = calloc((size_t)njoy, sizeof(*joys));
    unsigned frames = 0;
    if (!texture || (njoy && !joys)) goto sdl_done;
    for (int i = 0; i < njoy; i++) joys[i] = SDL_JoystickOpen(i);
    while (!stopped && (!limit || frames < (unsigned)limit)) {
        double start = now_ms(); SDL_Event e;
        while (SDL_PollEvent(&e)) event(&e);
        if (stopped) break;
        const uint8_t *p = step(buttons);
        if (!p || SDL_UpdateTexture(texture, NULL, p, WIDTH * 4) || SDL_RenderClear(renderer) || SDL_RenderCopy(renderer, texture, NULL, NULL)) goto sdl_done;
        SDL_RenderPresent(renderer); frames++;
        if (frames == 1) {
            printf("first_present_ms=%.3f video=%s\n", now_ms() - boot, SDL_GetCurrentVideoDriver());
            if (dump && !dump_ppm(p, dump)) goto sdl_done;
        }
        double delay = 1000.0 / 60.0 - (now_ms() - start);
        if (delay > 0) SDL_Delay((Uint32)delay);
    }
    result = 0;
sdl_done:
    printf("frames=%u\n", frames);
    if (joys) { for (int i = 0; i < njoy; i++) if (joys[i]) SDL_JoystickClose(joys[i]); free(joys); }
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (result) fprintf(stderr, "SDL: %s\n", SDL_GetError());
    SDL_Quit();
done:
    if (result) fprintf(stderr, "Runtime: %s\n", pocket_runtime_error());
    pocket_runtime_shutdown(); free(pack); free(js);
    struct rusage usage; getrusage(RUSAGE_SELF, &usage);
    printf("guest_tick_raster_max_ms=%.3f peak_rss_kib=%ld exit=%d\n", max_work_ms, usage.ru_maxrss, result);
    return result;
}
