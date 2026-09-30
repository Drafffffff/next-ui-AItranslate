/* Experimental Brick host for PocketJS Solid/QuickJS applications. */
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include "pocket_runtime.h"
#include "brick-services.h"
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
        case SDLK_ESCAPE: case SDLK_BACKSPACE: return POCKET_BTN_CROSS;
        case SDLK_x: return POCKET_BTN_TRIANGLE;
        default: return 0;
    }
}
static void event(const SDL_Event *e) {
    if (e->type == SDL_QUIT) stopped = 1;
    else if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
        uint32_t bit = key(e->key.keysym.sym);
        if (e->type == SDL_KEYDOWN) buttons |= bit; else buttons &= ~bit;
    } else if (e->type == SDL_JOYHATMOTION) {
        buttons &= ~0xf0U;
        if (e->jhat.value & SDL_HAT_UP) buttons |= POCKET_BTN_UP;
        if (e->jhat.value & SDL_HAT_RIGHT) buttons |= POCKET_BTN_RIGHT;
        if (e->jhat.value & SDL_HAT_DOWN) buttons |= POCKET_BTN_DOWN;
        if (e->jhat.value & SDL_HAT_LEFT) buttons |= POCKET_BTN_LEFT;
    } else if (e->type == SDL_JOYBUTTONDOWN || e->type == SDL_JOYBUTTONUP) {
        uint32_t bit = e->jbutton.button == 1 ? POCKET_BTN_CIRCLE : e->jbutton.button == 0 ? POCKET_BTN_CROSS : e->jbutton.button == 3 ? POCKET_BTN_TRIANGLE : 0;
        if (e->type == SDL_JOYBUTTONDOWN) buttons |= bit; else buttons &= ~bit;
        if (e->type == SDL_JOYBUTTONDOWN && e->jbutton.button == 8) stopped = 1;
    } else if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) buttons = 0;
}
static const uint8_t *step(uint32_t mask) {
    PocketRuntimeInput input = {0}; input.buttons = mask;
    double start = now_ms();
    if (!pocket_runtime_tick(&input)) return NULL;
    if (!strcmp(pocket_runtime_action_name(), "app.exit")) stopped = 1;
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
static int harness(int op) {
    int32_t result = -999;
    if (!pocket_runtime_harness_call(op, 0, &result)) return -999;
    return result;
}
static int wait_for(int op, int expected, double timeout_ms) {
    double deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        if (!step(0)) return 0;
        if (harness(op) == expected) return 1;
        SDL_Delay(1);
    }
    return 0;
}
static int self_test(const char *dump) {
    if (!pocket_runtime_harness_bind("__brickAcceptance") || !wait_for(0, 1, 3000)) return 1;
    const uint8_t *p = step(0); if (!p) return 1;
    uint32_t color; memcpy(&color, p + (210 * WIDTH + 50) * 4, 4);
    if (color != 0xff344d2eU) return 1;
    SDL_Event e; memset(&e, 0, sizeof(e)); e.type = SDL_JOYHATMOTION;
    e.jhat.value = SDL_HAT_DOWN; event(&e);
    for (int i = 0; i < 160; i++) if (!step(buttons)) return 1;
    if (harness(6) != 12) return 1;
    e.jhat.value = SDL_HAT_UP; event(&e);
    for (int i = 0; i < 180; i++) if (!step(buttons)) return 1;
    if (harness(6) != 1) return 1;
    e.jhat.value = SDL_HAT_CENTERED; event(&e); if (!step(buttons)) return 1;
    if (harness(2) != 1) return 1;
    /* Move the actual app while a delayed worker request is outstanding. */
    e.jhat.value = SDL_HAT_DOWN; event(&e);
    for (int i = 0; i < 10; i++) { if (!step(buttons)) return 1; SDL_Delay(1); }
    e.jhat.value = SDL_HAT_CENTERED; event(&e);
    if (!wait_for(1, 1, 10000)) { harness(10); fprintf(stderr, "Acceptance did not finish: %s\n", pocket_runtime_error()); return 1; }
    p = step(0); if (!p || (dump && !dump_ppm(p, dump))) return 1;
    if (harness(4) != 2) return 1;
    e.type = SDL_JOYBUTTONDOWN; e.jbutton.button = 0; event(&e);
    if (!step(buttons) || harness(9) != 1 || stopped) return 1;
    e.type = SDL_JOYBUTTONUP; event(&e); if (!step(buttons)) return 1;
    if (harness(11) != 1 || !step(0) || !step(0)) return 1;
    e.type = SDL_JOYBUTTONDOWN; e.jbutton.button = 1; event(&e);
    for (int i = 0; i < 5; i++) if (!step(buttons)) return 1;
    if (harness(13) != 1) return 1;
    if (dump) { char path[1200]; snprintf(path, sizeof(path), "%s.keyboard.ppm", dump); if (!dump_ppm(step(0), path)) return 1; }
    e.type = SDL_JOYBUTTONUP; event(&e); if (!step(buttons)) return 1;
    e.type = SDL_JOYBUTTONDOWN; e.jbutton.button = 0; event(&e); if (!step(buttons)) return 1;
    e.type = SDL_JOYBUTTONUP; event(&e); if (!step(buttons) || harness(9) != 1) return 1;
    if (harness(12) != 1 || !step(0) || !step(0)) return 1;
    if (dump) { char path[1200]; snprintf(path, sizeof(path), "%s.confirm.ppm", dump); if (!dump_ppm(step(0), path)) return 1; }
    e.type = SDL_JOYBUTTONDOWN; e.jbutton.button = 1; event(&e); if (!step(buttons)) return 1;
    e.type = SDL_JOYBUTTONUP; event(&e);
    if (!step(buttons) || harness(9) != 1 || harness(4) != 2) return 1;
    if (harness(8) != 1) return 1;
    for (int i = 0; i < 10; i++) { if (!step(0)) return 1; SDL_Delay(1); }
    e.type = SDL_JOYBUTTONDOWN; e.jbutton.button = 8; event(&e); if (!stopped) return 1;
    puts("PASS: full app navigation, background file/network/font jobs, timeout, cancellation, body limits, chat, persisted history, dynamic Chinese, keyboard A edge, clear confirmation cancel");
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
    if (test) setenv("POCKETJS_TEST", "1", 1);
    double boot = now_ms(); size_t js_size = 0, pack_size = 0;
    char *js = read_file("brick-app.js", &js_size);
    uint8_t *pack = read_file("brick-app.pak", &pack_size);
    if (!js || !pack) { fprintf(stderr, "Cannot read brick-app.js / brick-app.pak\n"); goto done; }
    if (!pocket_runtime_boot(js, js_size, pack, pack_size, WIDTH, HEIGHT)) goto done;
    printf("PocketJS Brick app / Solid + QuickJS\nupstream=%s guest_boot_ms=%.3f\n", POCKETJS_REV, now_ms() - boot);
    if (test) {
        result = self_test(dump);
        if (!result) {
            double shutdown_start = now_ms(); pocket_runtime_shutdown();
            double shutdown_time = now_ms() - shutdown_start;
            printf("active_request_shutdown_ms=%.3f\n", shutdown_time);
            if (shutdown_time > 1000) { result = 1; goto done; }
            buttons = 0; stopped = 0;
            if (!pocket_runtime_boot(js, js_size, pack, pack_size, WIDTH, HEIGHT) || !pocket_runtime_harness_bind("__brickAcceptance") || !wait_for(0, 1, 3000) || harness(4) != 2) result = 1;
            else puts("PASS: guest shutdown/restart restores saved history");
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
