#define _POSIX_C_SOURCE 200809L
#include "ma_netplay.h"
#include <dlfcn.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef NP_SDL
#include <SDL.h>
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
#endif
static enum retro_pixel_format pixel_format;
static unsigned char pixels[240*160*4];
static unsigned pixel_width, pixel_height;
static size_t pixel_pitch;
struct Hold { unsigned start, end, mask; };
static struct Hold holds[1024];
static unsigned hold_count;
static unsigned control_sequence, control_until, control_mask;
static FILE *input_history;

static const char *serial = "mul_poke";
static const char *system_dir = ".";
static unsigned frames, video_frames;
static uint32_t button_mask;
static void *core;
static void (*run)(void);
static void (*deinit)(void);
static void (*unload)(void);
static void *(*memory)(unsigned);
static size_t (*memory_size)(unsigned);
static void core_log(enum retro_log_level level, const char *format, ...) {
    if (level < RETRO_LOG_INFO) return;
    va_list args;
    va_start(args, format);
    if (!strcmp(getenv("NP_CORE_LOG"), "link")) {
        char message[4096];
        vsnprintf(message, sizeof(message), format, args);
        if (level >= RETRO_LOG_WARN || strstr(message, "GBA Wi-Fi Link"))
            fputs(message, stderr);
    } else vfprintf(stderr, format, args);
    va_end(args);
}
static bool env(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (!getenv("NP_CORE_LOG")) return false;
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE: {
        struct retro_netpacket_callback cb = *(struct retro_netpacket_callback *)data;
        if (getenv("NP_PROTOCOL")) cb.protocol_version = getenv("NP_PROTOCOL");
        return Netplay_setInterface(&cb);
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = system_dir; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        if (!strcmp(v->key, "gpsp_serial")) v->value = serial;
        else if (!strcmp(v->key, "gpsp_boot_mode")) v->value = "game";
        else if (!strcmp(v->key, "gpsp_drc")) v->value = getenv("NP_DYNAREC") ? "enabled" : "disabled";
        else v->value = NULL;
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixel_format = *(enum retro_pixel_format *)data;
        return true;
    default: return false;
    }
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch) {
    video_frames++;
    if (data && w <= 240 && h <= 160 && pitch*h <= sizeof(pixels)) {
        memcpy(pixels, data, pitch*h); pixel_width=w; pixel_height=h; pixel_pitch=pitch;
        if (getenv("NP_CAPTURE_PPM") && frames && frames % 60 == 0) {
            unsigned char rgb[240*160*3];
            for (unsigned y=0;y<h;y++) for (unsigned x=0;x<w;x++) {
                uint32_t c=0; unsigned at=(y*w+x)*3;
                if (pixel_format==RETRO_PIXEL_FORMAT_XRGB8888) {
                    memcpy(&c,pixels+y*pitch+x*4,4);
                    rgb[at]=c>>16;rgb[at+1]=c>>8;rgb[at+2]=c;
                } else {
                    uint16_t v;memcpy(&v,pixels+y*pitch+x*2,2);
                    rgb[at]=((v>>11)&31)*255/31;
                    rgb[at+1]=((v>>5)&63)*255/63;rgb[at+2]=(v&31)*255/31;
                }
            }
            char path[1024];snprintf(path,sizeof(path),"%s.tmp",getenv("NP_CAPTURE_PPM"));
            FILE *shot=fopen(path,"wb");
            if (shot) {
                fprintf(shot,"P6\n%u %u\n255\n",w,h);fwrite(rgb,1,w*h*3,shot);fclose(shot);
                rename(path,getenv("NP_CAPTURE_PPM"));
                printf("SNAPSHOT frame=%u sent=%llu received=%llu\n",frames,
                    (unsigned long long)Netplay_sent(),(unsigned long long)Netplay_received());
            }
        }
#ifdef NP_SDL
        unsigned capture_every=getenv("NP_CAPTURE_EVERY") ? (unsigned)atoi(getenv("NP_CAPTURE_EVERY")) : 0;
        if (capture_every && getenv("NP_CAPTURE") && frames && frames % capture_every == 0) {
            char path[1024];
            if (getenv("NP_LIVE_CAPTURE")) snprintf(path,sizeof(path),"%s.tmp",getenv("NP_CAPTURE"));
            else snprintf(path,sizeof(path),"%s-%06u.bmp",getenv("NP_CAPTURE"),frames);
            SDL_Surface *shot=SDL_CreateRGBSurfaceWithFormatFrom(pixels,(int)w,(int)h,
                pixel_format==RETRO_PIXEL_FORMAT_XRGB8888?32:16,(int)pitch,
                pixel_format==RETRO_PIXEL_FORMAT_XRGB8888?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565);
            if (shot) {
                SDL_SaveBMP(shot,path); SDL_FreeSurface(shot);
                if (getenv("NP_LIVE_CAPTURE")) {
                    rename(path,getenv("NP_CAPTURE"));
                    printf("SNAPSHOT frame=%u sent=%llu received=%llu\n",frames,
                           (unsigned long long)Netplay_sent(),(unsigned long long)Netplay_received());
                }
            }
        }
        if (renderer) {
            if (!texture) texture = SDL_CreateTexture(renderer,
                pixel_format == RETRO_PIXEL_FORMAT_XRGB8888 ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_RGB565,
                SDL_TEXTUREACCESS_STREAMING, (int)w, (int)h);
            if (texture) { SDL_UpdateTexture(texture,NULL,data,(int)pitch); SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer,texture,NULL,NULL); SDL_RenderPresent(renderer); }
        }
#endif
    }
}
static void audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *p, size_t n) { (void)p; return n; }
static void input_poll(void) {}
static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)port; (void)device; (void)index;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)button_mask;
    return (int16_t)((button_mask >> id) & 1U);
}
static void symbol(void *out, const char *name) {
    void *ptr = dlsym(core, name);
    if (!ptr) { fprintf(stderr, "Missing %s\n", name); exit(2); }
    memcpy(out, &ptr, sizeof(ptr));
}
#define SYM(var, name) symbol(&(var), name)
int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "core-runner CORE ROM host|join IP PORT [MODE] [FRAMES]\n"); return 2; }
    setbuf(stdout, NULL);
    if (getenv("NP_INPUT_LOG")) {
        input_history=fopen(getenv("NP_INPUT_LOG"),"w");
        if (!input_history) { perror("input history"); return 2; }
    }
    if (getenv("NP_SYSTEM")) system_dir=getenv("NP_SYSTEM");
    if (getenv("NP_SCRIPT")) {
        FILE *script=fopen(getenv("NP_SCRIPT"),"r");
        if (!script) { perror("input script"); return 2; }
        while (hold_count < 1024 && fscanf(script,"%u %u %u", &holds[hold_count].start,
               &holds[hold_count].end, &holds[hold_count].mask)==3) hold_count++;
        fclose(script);
    }
#ifdef NP_SDL
    if (getenv("NP_WINDOW")) {
        if (SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr,"%s\n",SDL_GetError()); return 2; }
        window=SDL_CreateWindow(argv[3],!strcmp(argv[3],"host")?100:650,150,480,320,0);
        renderer=SDL_CreateRenderer(window,-1,0);
        if (!window || !renderer) return 2;
    }
#endif
    if (argc > 6) serial = argv[6];
    unsigned limit = argc > 7 ? (unsigned)strtoul(argv[7], NULL, 10) : 1200;
    FILE *file = fopen(argv[2], "rb");
    if (!file) { perror("ROM"); return 2; }
    fseek(file, 0, SEEK_END); long size = ftell(file); rewind(file);
    if (size <= 0) return 2;
    unsigned char *rom = malloc((size_t)size), sha[32]; unsigned sha_size;
    if (!rom || fread(rom, 1, (size_t)size, file) != (size_t)size) return 2;
    fclose(file);
    EVP_Digest(rom, (size_t)size, sha, &sha_size, EVP_sha256(), NULL);
    core = dlopen(argv[1], RTLD_NOW);
    if (!core) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    void (*init)(void), (*set_env)(retro_environment_t);
    void (*set_video)(retro_video_refresh_t), (*set_audio)(retro_audio_sample_t);
    void (*set_batch)(retro_audio_sample_batch_t), (*set_poll)(retro_input_poll_t), (*set_input)(retro_input_state_t);
    bool (*load)(const struct retro_game_info *);
    SYM(init,"retro_init"); SYM(deinit,"retro_deinit"); SYM(run,"retro_run");
    SYM(unload,"retro_unload_game"); SYM(load,"retro_load_game");
    SYM(memory,"retro_get_memory_data"); SYM(memory_size,"retro_get_memory_size");
    SYM(set_env,"retro_set_environment"); SYM(set_video,"retro_set_video_refresh");
    SYM(set_audio,"retro_set_audio_sample"); SYM(set_batch,"retro_set_audio_sample_batch");
    SYM(set_poll,"retro_set_input_poll"); SYM(set_input,"retro_set_input_state");
    set_env(env); set_video(video); set_audio(audio); set_batch(audio_batch); set_poll(input_poll); set_input(input);
    init();
    struct retro_game_info info = {.path=argv[2], .data=rom, .size=(size_t)size};
    if (!load(&info)) { fprintf(stderr,"Core refused ROM\n"); return 2; }
    if (getenv("NP_SAVE")) {
        FILE *save=fopen(getenv("NP_SAVE"),"rb");
        if (save) { size_t sz=memory_size(RETRO_MEMORY_SAVE_RAM); void *data=memory(RETRO_MEMORY_SAVE_RAM);
            if (data && sz) { size_t got=fread(data,1,sz,save); (void)got; } fclose(save); }
    }
    unsigned char core_sha[32];
    EVP_MD_CTX *digest=EVP_MD_CTX_new();
    FILE *image=fopen(argv[1], "rb"); unsigned char block[16384]; size_t n;
    if (!digest || !image || EVP_DigestInit_ex(digest,EVP_sha256(),NULL)!=1) return 2;
    while ((n=fread(block,1,sizeof(block),image))) if (EVP_DigestUpdate(digest,block,n)!=1) return 2;
    if (ferror(image) || EVP_DigestFinal_ex(digest,core_sha,&sha_size)!=1) return 2;
    fclose(image); EVP_MD_CTX_free(digest);
    if (getenv("NP_CORE_MISMATCH")) core_sha[0]^=1;
    Netplay_identity(sha, core_sha, serial);
    bool host = !strcmp(argv[3], "host");
    bool ok = getenv("NP_ROOM") ? Netplay_relay(argv[4], (uint16_t)atoi(argv[5]), getenv("NP_ROOM"), host) :
              host ? Netplay_host((uint16_t)atoi(argv[5])) : Netplay_join(argv[4], (uint16_t)atoi(argv[5]));
    if (!ok) { fprintf(stderr,"%s\n",Netplay_status()); return 2; }
    enum NPState previous = NP_IDLE;
    struct timespec tick = {.tv_nsec=getenv("NP_FAST") ? 1000000 : 16666667};
    unsigned waiting = 0, pause_ticks = 0;
    bool pause_requested = false, pause_released = false;
    struct timespec pause_time = {0};
    while (frames < limit && Netplay_state() != NP_ERROR) {
        struct timespec iteration_start;
        clock_gettime(CLOCK_MONOTONIC, &iteration_start);
        if (getenv("NP_STOP_FILE") && access(getenv("NP_STOP_FILE"),F_OK)==0) break;
        Netplay_poll();
        if (host && getenv("NP_PAUSE") && frames >= (unsigned)(getenv("NP_PAUSE_FRAME") ? atoi(getenv("NP_PAUSE_FRAME")) : 60) && !pause_requested) {
            Netplay_pause(true); pause_requested = true;
            clock_gettime(CLOCK_MONOTONIC, &pause_time);
        }
        if (pause_requested && !pause_released) {
            struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
            double elapsed = t.tv_sec - pause_time.tv_sec + (t.tv_nsec - pause_time.tv_nsec) / 1e9;
            if (elapsed >= 2) { Netplay_pause(false); pause_released = true; }
        }
        if (Netplay_paused()) pause_ticks++;
        if (Netplay_state() != previous) {
            printf("state=%d %s\n", Netplay_state(), Netplay_status()); previous = Netplay_state();
        }
        if (getenv("NP_CONTROL")) {
            FILE *control=fopen(getenv("NP_CONTROL"),"r"); unsigned sequence,mask,duration;
            if (control) {
                if (fscanf(control,"%u %u %u",&sequence,&mask,&duration)==3 && sequence!=control_sequence) {
                    control_sequence=sequence;control_mask=mask;control_until=frames+duration;
                    printf("INPUT %u %u %u\n",frames,control_until,mask);
                    if (input_history) { fprintf(input_history,"%u %u %u\n",frames,control_until,mask);fflush(input_history); }
                }
                fclose(control);
            }
        }
        button_mask=frames<control_until ? control_mask : 0;
        for (unsigned i=0;i<hold_count;i++) if (frames>=holds[i].start && frames<holds[i].end) button_mask|=holds[i].mask;
#ifdef NP_SDL
        if (window) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type==SDL_QUIT) goto finished;
                if (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F12 && getenv("NP_CAPTURE")) {
                    SDL_Surface *shot=SDL_CreateRGBSurfaceWithFormatFrom(pixels,(int)pixel_width,(int)pixel_height,
                        pixel_format==RETRO_PIXEL_FORMAT_XRGB8888?32:16,(int)pixel_pitch,
                        pixel_format==RETRO_PIXEL_FORMAT_XRGB8888?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565);
                    if (shot) { SDL_SaveBMP(shot,getenv("NP_CAPTURE")); SDL_FreeSurface(shot); }
                }
            }
            const Uint8 *keys=SDL_GetKeyboardState(NULL);
            if (keys[SDL_SCANCODE_X]) button_mask|=1U<<RETRO_DEVICE_ID_JOYPAD_A;
            if (keys[SDL_SCANCODE_Z]) button_mask|=1U<<RETRO_DEVICE_ID_JOYPAD_B;
            if (keys[SDL_SCANCODE_RETURN]) button_mask|=1U<<RETRO_DEVICE_ID_JOYPAD_START;
            if (keys[SDL_SCANCODE_UP]) button_mask|=1U<<RETRO_DEVICE_ID_JOYPAD_UP;
            if (keys[SDL_SCANCODE_DOWN]) button_mask|=1U<<RETRO_DEVICE_ID_JOYPAD_DOWN;
            if (keys[SDL_SCANCODE_LEFT]) button_mask|=1U<<RETRO_DEVICE_ID_JOYPAD_LEFT;
            if (keys[SDL_SCANCODE_RIGHT]) button_mask|=1U<<RETRO_DEVICE_ID_JOYPAD_RIGHT;
        }
#endif
        if (Netplay_active() && !Netplay_paused()) { run(); frames++; }
        else if (++waiting > 1800) break;
        if (getenv("NP_FAST_UNTIL")) tick.tv_nsec=frames<(unsigned)atoi(getenv("NP_FAST_UNTIL")) ? 1000000 : 16666667;
        struct timespec sleep_time = tick;
        if (getenv("NP_REALTIME")) {
            /* Match a front end's total frame interval, rather than adding
             * 16.7ms on top of the interpreter's execution time. Do not run
             * catch-up frames, which would distort RFU network timing. */
            struct timespec ended;
            clock_gettime(CLOCK_MONOTONIC, &ended);
            int64_t elapsed = (int64_t)(ended.tv_sec - iteration_start.tv_sec) * 1000000000LL +
                              ended.tv_nsec - iteration_start.tv_nsec;
            sleep_time.tv_nsec = elapsed < tick.tv_nsec ? tick.tv_nsec - elapsed : 0;
        }
        if (sleep_time.tv_nsec) {
            if (getenv("NP_NETWORK_WAIT")) Netplay_wait((unsigned)(sleep_time.tv_nsec / 1000));
            else nanosleep(&sleep_time, NULL);
        }
    }
#ifdef NP_SDL
finished:
    if (getenv("NP_CAPTURE") && pixel_width && pixel_height) {
        SDL_Surface *shot=SDL_CreateRGBSurfaceWithFormatFrom(pixels,(int)pixel_width,(int)pixel_height,
            pixel_format==RETRO_PIXEL_FORMAT_XRGB8888?32:16,(int)pixel_pitch,
            pixel_format==RETRO_PIXEL_FORMAT_XRGB8888?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565);
        if (shot) { SDL_SaveBMP(shot,getenv("NP_CAPTURE")); SDL_FreeSurface(shot); }
    }
#endif
    uint32_t *ram = memory(RETRO_MEMORY_SYSTEM_RAM);
    unsigned counter = ram && memory_size(RETRO_MEMORY_SYSTEM_RAM) >= 8 ? ram[1] : 0;
    printf("RESULT frames=%u video=%u sent=%llu received=%llu marker=%08x counter=%u paused_ticks=%u status=%s\n",
           frames,video_frames,(unsigned long long)Netplay_sent(),(unsigned long long)Netplay_received(),
           ram ? ram[0] : 0,counter,pause_ticks,Netplay_status());
    if (!getenv("NP_GAME")) printf("DUEL local_hp=%u peer_hp=%u\n",ram ? ram[4] : 0,ram ? ram[5] : 0);
    else puts("GAMEPLAY_STATUS: runner verifies emulation only; multiplayer requires game evidence");
    bool passed = frames > 60 && Netplay_sent() > 10 && Netplay_received() > 10 && counter > 10 &&
                  ram && ram[0] == (host ? 0x5678U : 0x1234U);
    if (getenv("NP_DUEL")) passed = passed && ram && ram[4]==(host ? 19U : 18U) && ram[5]==(host ? 18U : 19U);
    if (getenv("NP_GAME")) passed = video_frames > 60 && frames > 60;
    if (getenv("NP_SAVE")) {
        FILE *save=fopen(getenv("NP_SAVE"),"wb"); size_t sz=memory_size(RETRO_MEMORY_SAVE_RAM);
        void *data=memory(RETRO_MEMORY_SAVE_RAM);
        if (!save || (sz && (!data || fwrite(data,1,sz,save)!=sz))) return 2;
        fclose(save);
    }
    Netplay_stop("Test completed"); unload(); deinit(); dlclose(core); free(rom);
#ifdef NP_SDL
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
#endif
    if (input_history) fclose(input_history);
    return passed ? 0 : 1;
}
