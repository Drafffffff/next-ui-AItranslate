#define _GNU_SOURCE
#include <dlfcn.h>
#include <SDL.h>
#include <signal.h>

static volatile sig_atomic_t quit_requested;
static void request_quit(int signal_number) {
    (void)signal_number;
    quit_requested = 1;
}
static void install_quit_handler(void) {
    struct sigaction action = {0};
    action.sa_handler = request_quit;
    sigaction(SIGUSR1, &action, NULL);
}

/* Brick's SDL supports the display and pad, but omits sensors. PICO-8 asks
 * for SDL_INIT_EVERYTHING. Keep the device SDL and omit only this subsystem. */
int SDL_Init(Uint32 flags) {
    install_quit_handler();
    int (*next_init)(Uint32) = dlsym(RTLD_NEXT, "SDL_Init");
    if (!next_init) return -1;
    return next_init(flags & ~SDL_INIT_SENSOR);
}

int SDL_InitSubSystem(Uint32 flags) {
    install_quit_handler();
    int (*next_init)(Uint32) = dlsym(RTLD_NEXT, "SDL_InitSubSystem");
    if (!next_init) return -1;
    return next_init(flags & ~SDL_INIT_SENSOR);
}

/* Request normal app shutdown through its SDL event loop. Never call SDL
 * from a signal handler: it is delivered here on the app's own thread. */
int SDL_PollEvent(SDL_Event *event) {
    int (*next_poll)(SDL_Event *) = dlsym(RTLD_NEXT, "SDL_PollEvent");
    if (quit_requested && event) {
        quit_requested = 0;
        SDL_zero(*event);
        event->type = SDL_QUIT;
        return 1;
    }
    return next_poll ? next_poll(event) : 0;
}
