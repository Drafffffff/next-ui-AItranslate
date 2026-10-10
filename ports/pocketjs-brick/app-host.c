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
#include "brick-hardware.h"
#include "pocket_spec.h"
#define WIDTH 1024
#define HEIGHT 768
static volatile sig_atomic_t stopped;
static uint32_t buttons, pressed;
static uint32_t frame_buttons(void) {
    uint32_t mask = buttons | pressed; pressed = 0; return mask;
}
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
        case SDLK_x: return POCKET_BTN_TRIANGLE; case SDLK_z: return POCKET_BTN_SQUARE;
        case SDLK_q: return POCKET_BTN_LTRIGGER; case SDLK_e: return POCKET_BTN_RTRIGGER;
        case SDLK_TAB: return POCKET_BTN_SELECT; case SDLK_p: return POCKET_BTN_START;
        default: return 0;
    }
}
static void event(const SDL_Event *e) {
    if (e->type == SDL_QUIT) stopped = 1;
    else if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
        uint32_t bit = key(e->key.keysym.sym);
        if (e->type == SDL_KEYDOWN) { buttons |= bit; if (!e->key.repeat) pressed |= bit; } else buttons &= ~bit;
    } else if (e->type == SDL_JOYHATMOTION) {
        buttons &= ~0xf0U;
        if (e->jhat.value & SDL_HAT_UP) buttons |= POCKET_BTN_UP;
        if (e->jhat.value & SDL_HAT_RIGHT) buttons |= POCKET_BTN_RIGHT;
        if (e->jhat.value & SDL_HAT_DOWN) buttons |= POCKET_BTN_DOWN;
        if (e->jhat.value & SDL_HAT_LEFT) buttons |= POCKET_BTN_LEFT;
    } else if (e->type == SDL_JOYBUTTONDOWN || e->type == SDL_JOYBUTTONUP) {
        static const uint32_t map[]={POCKET_BTN_CROSS,POCKET_BTN_CIRCLE,POCKET_BTN_SQUARE,POCKET_BTN_TRIANGLE,POCKET_BTN_LTRIGGER,POCKET_BTN_RTRIGGER,POCKET_BTN_SELECT,POCKET_BTN_START};
        uint32_t bit = e->jbutton.button < 8 ? map[e->jbutton.button] : 0;
        if (e->type == SDL_JOYBUTTONDOWN) { buttons |= bit; pressed |= bit; } else buttons &= ~bit;
        if (e->type == SDL_JOYBUTTONDOWN && e->jbutton.button == 8) stopped = 1;
    } else if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) buttons = pressed = 0;
}
static const uint8_t *step(uint32_t mask) {
    PocketRuntimeInput input = {0}; input.buttons = mask;
    double start = now_ms();
    if (!pocket_runtime_tick(&input)) return NULL;
    if (!strcmp(pocket_runtime_action_name(), "app.exit")) stopped = 1;
    const uint8_t *p = brick_canvas_render(pocket_runtime_render());
    double elapsed = now_ms() - start; if (elapsed > max_work_ms) max_work_ms = elapsed;brick_services_frame(elapsed);brick_hardware_frame(elapsed,0);
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
    SDL_Event e; memset(&e, 0, sizeof(e));
    e.type = SDL_KEYDOWN; e.key.keysym.sym = SDLK_DOWN; event(&e);
    e.type = SDL_KEYUP; event(&e);
    if (!step(frame_buttons()) || !step(frame_buttons()) || harness(6) != 2) return 1;
    e.type = SDL_KEYDOWN; e.key.keysym.sym = SDLK_UP; event(&e);
    e.type = SDL_KEYUP; event(&e);
    if (!step(frame_buttons()) || !step(frame_buttons()) || harness(6) != 1) return 1;
    e.type = SDL_JOYHATMOTION;
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
    int test = 0, creative_test = 0, creative_bench = 0, hardware_test = 0, workshop_test = 0, workshop_live = 0, workshop_recover = 0, workshop_ready = 0, workshop_ui = 0, workshop_dictation = 0, workshop_controls = 0, workshop_models = 0, workshop_avatar = 0, workshop_transcript=0,workshop_stream=0,workshop_question=0,workshop_return=0,dummy = 0, limit = 0, result = 1;
    int workshop_eval = 0,workshop_share=0,workshop_terminal=0,workshop_history=0;
    const char *dump = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--self-test")) test = 1;
        else if (!strcmp(argv[i], "--creative-test")) creative_test = 1;
        else if (!strcmp(argv[i], "--creative-benchmark")) creative_bench = 1;
        else if (!strcmp(argv[i], "--hardware-test")) hardware_test = 1;
        else if (!strcmp(argv[i], "--workshop-history-stress")&&i+1<argc){workshop_history=atoi(argv[++i]);if(workshop_history!=10&&workshop_history!=30&&workshop_history!=60)return 2;}
        else if (!strcmp(argv[i], "--workshop-share-test")) workshop_share=1;
        else if (!strcmp(argv[i], "--workshop-terminal-test")) workshop_terminal=1;
        else if (!strcmp(argv[i], "--workshop-test")) workshop_test = 1;
        else if (!strcmp(argv[i], "--workshop-live")) workshop_test = workshop_live = 1;
        else if (!strcmp(argv[i], "--workshop-recover")) workshop_recover = 1;
        else if (!strcmp(argv[i], "--workshop-ready")) workshop_ready = 1;
        else if (!strcmp(argv[i], "--workshop-ui-test")) workshop_ui = 1;
        else if (!strcmp(argv[i], "--workshop-dictation-test")) workshop_dictation = 1;
        else if (!strcmp(argv[i], "--workshop-controls-test")) workshop_controls = 1;
        else if (!strcmp(argv[i], "--workshop-question-test")) workshop_question=1;
        else if (!strcmp(argv[i], "--workshop-question-wait")) workshop_question=2;
        else if (!strcmp(argv[i], "--workshop-question-resume")) workshop_question=3;
        else if (!strcmp(argv[i], "--workshop-question-cancel-test")) workshop_question=5;
        else if (!strcmp(argv[i], "--workshop-question-voice-test")) workshop_question=4;
        else if (!strcmp(argv[i], "--workshop-models-test")) workshop_models = 1;
        else if (!strcmp(argv[i], "--workshop-avatar-test")) workshop_avatar = 1;
        else if (!strcmp(argv[i],"--workshop-transcript-test"))workshop_transcript=1;
        else if (!strcmp(argv[i],"--workshop-stream-test"))workshop_stream=1;
        else if (!strcmp(argv[i],"--workshop-return-seed"))workshop_return=1;
        else if (!strcmp(argv[i],"--workshop-return-test"))workshop_return=2;
        else if (!strcmp(argv[i],"--workshop-return-fallback"))workshop_return=3;
        else if (!strcmp(argv[i],"--workshop-return-baseline"))workshop_return=4;
        else if (!strcmp(argv[i],"--workshop-generation-perf"))workshop_return=5;
        else if (!strcmp(argv[i],"--workshop-generation-voice-perf"))workshop_return=6;
        else if (!strcmp(argv[i],"--workshop-version-test"))workshop_return=7;
        else if (!strcmp(argv[i],"--workshop-eval"))workshop_eval=1;
        else if (!strcmp(argv[i], "--dummy-video")) dummy = 1;
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end; long n = strtol(argv[++i], &end, 10);
            if (*end || n < 1 || n > 1000000) return 2;
            limit = (int)n;
        } else return 2;
    }
    if (test || workshop_controls) setenv("POCKETJS_TEST", "1", 1);
    double boot = now_ms(); size_t js_size = 0, pack_size = 0;
    char *js = read_file("brick-app.js", &js_size);
    uint8_t *pack = read_file("brick-app.pak", &pack_size);
    if (!js || !pack) { fprintf(stderr, "Cannot read brick-app.js / brick-app.pak\n"); goto done; }
    if (!pocket_runtime_boot(js, js_size, pack, pack_size, WIDTH, HEIGHT)) goto done;
    printf("PocketJS Brick app / Solid + QuickJS\nupstream=%s guest_boot_ms=%.3f\n", POCKETJS_REV, now_ms() - boot);
    /* Developer runner only: the separately bundled evaluation entry drives the
       production Agent and native services. No extra tools enter the Agent. */
    if(workshop_eval){
        if(!pocket_runtime_harness_bind("__workshopEvaluation")||!wait_for(0,1,3600000))goto done;
        if(dump&&!dump_ppm(step(0),dump))goto done;
        result=harness(1)?0:1;goto done;
    }
    if (workshop_test || workshop_recover || workshop_ready || workshop_ui || workshop_dictation || workshop_controls || workshop_models || workshop_avatar||workshop_transcript||workshop_stream||workshop_question||workshop_return||workshop_share||workshop_terminal||workshop_history) {
        if (!pocket_runtime_harness_bind("__workshopAcceptance") || !wait_for(0,1,10000)) goto done;
        if(workshop_history){
            max_work_ms=0;double begin=now_ms();int op=workshop_history==10?90:workshop_history==30?91:92;
            if(harness(op)!=1||!wait_for(93,1,180000)||harness(94)!=1||!wait_for(13,1,10000)||!harness(42))goto done;
            if(dump&&!dump_ppm(step(0),dump))goto done;
            printf("PASS history_stress: mib=%d messages=%d load_ms=%d elapsed_ms=%.3f tick_raster_max_ms=%.3f\n",workshop_history,harness(95),harness(96),now_ms()-begin,max_work_ms);result=0;goto done;
        }
        if(workshop_terminal){
            if(harness(86)!=1||!wait_for(87,1,10000)||harness(88)!=1)goto done;
            puts("PASS terminal: isolated script, read-only system snapshot, path denial and native cancellation");result=0;goto done;
        }
        if(workshop_share){
            if(harness(80)!=1||!wait_for(3,1,120000)||harness(81)!=1||harness(8)){harness(84);if(dump)dump_ppm(step(0),dump);goto done;}
            if(dump&&!dump_ppm(step(0),dump))goto done;
            puts("PASS sharing: native upload, published cart and QR page");result=0;goto done;
        }
        if(workshop_return){
            if(workshop_return==7){
                if(harness(9)!=1||!wait_for(3,1,10000)||!wait_for(13,1,10000))goto done;
                int revision=harness(4);
                if(!step(POCKET_BTN_LTRIGGER)||!step(0)||harness(83)<2)goto done;
                if(dump&&!dump_ppm(step(0),dump))goto done;
                if(!step(POCKET_BTN_DOWN)||!step(0)||!step(POCKET_BTN_CIRCLE)||!step(0)||!wait_for(3,1,10000)||harness(8)||harness(4)!=revision+1)goto done;
                puts("PASS versions: controller opens picker, selects previous version, restores unique ROM with a new revision");result=0;goto done;
            }
            if(workshop_return==5||workshop_return==6){
                if(harness(9)!=1||!wait_for(3,1,10000)||!wait_for(13,1,10000))goto done;
                max_work_ms=0;double begin=now_ms();int revision=harness(4);
                if(workshop_return==6){
                    if(!step(POCKET_BTN_CIRCLE))goto done;
                    for(int i=0;i<50;i++){if(!step(POCKET_BTN_CIRCLE))goto done;SDL_Delay(10);}
                    if(!step(0)||!wait_for(4,revision+1,240000))goto done;
                }else if(harness(6)!=1)goto done;
                if(!wait_for(3,1,240000)||!wait_for(13,1,10000)||harness(8)||harness(4)!=revision+1)goto done;
                printf("PASS generation_perf: elapsed_ms=%.3f tick_raster_max_ms=%.3f indexed=%d revision=%d\n",now_ms()-begin,max_work_ms,harness(40),harness(4));
                result=0;goto done;
            }
            if(workshop_return==4){
                if(!wait_for(10,1,10000)||!wait_for(13,1,10000))goto done;
                double deadline=now_ms()+10000;
                while(!harness(42)&&now_ms()<deadline){if(!step(0))goto done;SDL_Delay(1);}
            }
            double first=now_ms()-boot;
            if(workshop_return==1){
                if(harness(9)!=1||!wait_for(3,1,10000)||!wait_for(13,1,10000)||harness(5)!=1||!wait_for(3,1,10000)||harness(8))goto done;
                printf("PASS return cache saved: rows=%d indexed=%d\n",harness(42),harness(40));
            }else{
                if((workshop_return==2&&!harness(70))||(workshop_return==3&&harness(70))||harness(10)!=1||!harness(42))goto done;
                if(workshop_return==2&&(harness(71)||harness(40)||harness(3)))goto done;
                if(dump&&!dump_ppm(step(0),dump))goto done;
                printf("return_first_result_ms=%.3f cached=%d rows=%d cached_page_ms=%d\n",first,harness(70),harness(42),harness(72));
                if(!wait_for(3,1,10000)||!wait_for(13,1,10000)||harness(8))goto done;
                printf("PASS return restored: total_ms=%.3f project_load_ms=%d indexed=%d rows=%d\n",now_ms()-boot,harness(73),harness(40),harness(42));
            }
            result=0;goto done;
        }
        if(workshop_transcript){
            int uploads=harness(43),jobs=harness(47);double t=now_ms();char path[1400];
            if(harness(9)!=1||!wait_for(3,1,10000)||!wait_for(13,1,10000))goto done;
            printf("transcript_ready failed=%d indexed=%d rows=%d wraps=%d tail_marker=%d\n",harness(8),harness(40),harness(42),harness(41),harness(53));
            if(harness(8)&&dump){snprintf(path,sizeof(path),"%s.failure.ppm",dump);dump_ppm(step(0),path);}
            if(harness(8)||harness(40)<8000||harness(42)>24||harness(41)>12||!harness(53))goto done;
            printf("transcript_open_ms=%.3f indexed_messages=%d visible_rows=%d wrap_jobs=%d native_text_jobs=%d atlas_uploads=%d\n",now_ms()-t,harness(40),harness(42),harness(41),harness(47)-jobs,harness(43)-uploads);
            if(dump){snprintf(path,sizeof(path),"%s.tail.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            if(!step(POCKET_BTN_LEFT)||!step(0)||!wait_for(13,1,10000))goto done;
            printf("oldest_marker=%d position=%d rows=%d wraps=%d\n",harness(54),harness(11),harness(42),harness(41));
            if(!harness(54))goto done;
            int indexed=harness(40);double total=0,peak=0;
            for(int i=0;i<20;i++){t=now_ms();if(!step(POCKET_BTN_DOWN)||!step(0)||!wait_for(13,1,10000)||harness(8))goto done;double ms=now_ms()-t;total+=ms;if(ms>peak)peak=ms;}
            for(int i=0;i<2;i++){if(!step(POCKET_BTN_UP)||!step(0)||!wait_for(13,1,10000)||!step(POCKET_BTN_DOWN)||!step(0)||!wait_for(13,1,10000))goto done;}
            uploads=harness(43);jobs=harness(41);
            for(int i=0;i<10;i++){if(!step(POCKET_BTN_UP)||!step(0)||!wait_for(13,1,10000)||!step(POCKET_BTN_DOWN)||!step(0)||!wait_for(13,1,10000))goto done;}
            printf("warm_index=%d uploads_delta=%d wraps_delta=%d chars=%d windows=%d\n",harness(40),harness(43)-uploads,harness(41)-jobs,harness(45),harness(46));
            if(harness(40)!=indexed||harness(43)!=uploads||harness(41)-jobs>2||harness(45)>131072||harness(46)>64)goto done;
            printf("page_navigation_mean_ms=%.3f peak_ms=%.3f warm_wrap_jobs=%d warm_atlas_uploads=%d cached_text_chars=%d cached_windows=%d\n",total/20,peak,harness(41)-jobs,harness(43)-uploads,harness(45),harness(46));
            if(harness(50)!=1||!wait_for(13,1,10000)||harness(8))goto done;
            if(dump){snprintf(path,sizeof(path),"%s.error.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            if(harness(51)!=1||!wait_for(13,1,10000)||harness(8))goto done;
            for(int i=0;i<24;i++)if(!step(POCKET_BTN_DOWN)||!step(0)||!wait_for(13,1,10000)||harness(8)||harness(42)>24)goto done;
            if(dump){snprintf(path,sizeof(path),"%s.details.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            jobs=harness(47);for(int i=0;i<80;i++)if(!step(0))goto done;if(harness(47)!=jobs)goto done;
            if(!step(POCKET_BTN_CROSS)||!step(0)||harness(10)!=0)goto done;
            result=0;puts("PASS: long transcript visible-only layout, tail/oldest, cached scrolling, compact error/full paged details, no idle/off-page text work");goto done;
        }
        if(workshop_stream){
            if(harness(1)!=1||!wait_for(3,1,10000)||harness(2)!=1||!wait_for(55,1,10000)||!wait_for(56,1,10000)||harness(3)!=0)goto done;
            if(dump){char path[1400];snprintf(path,sizeof(path),"%s.live.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            int layouts=harness(44);double t=now_ms();
            for(int i=0;i<70;i++){if(!step(0))goto done;SDL_Delay(2);}
            printf("live_stream_window_ms=%.3f layout_commits=%d\n",now_ms()-t,harness(44)-layouts);
            if(harness(44)-layouts>8||!wait_for(3,1,30000)||!wait_for(13,1,10000)||harness(8)||harness(4)!=1||harness(55))goto done;
            if(dump&&!dump_ppm(step(0),dump))goto done;
            result=0;puts("PASS: native SSE text visible before HTTP completion, coalesced layouts, completed tool publication and stored reply");goto done;
        }
        if(workshop_avatar){
            if(harness(10)!=0||harness(9)!=1||!wait_for(3,1,10000)||!wait_for(13,1,10000)||harness(25)<1||harness(27)!=1)goto done;
            double total=0,peak=0;int samples=0,before=harness(24);char path[1400];
            for(int state=0;state<6;state++){
                if(harness(30+state)!=1)goto done;
                for(int i=0;i<(state==2?720:72);i++){
                    double t=now_ms();if(!step(0))goto done;double elapsed=now_ms()-t;total+=elapsed;if(elapsed>peak)peak=elapsed;samples++;
                    if(dump&&(i==24||i==48||i==624||i==648)){snprintf(path,sizeof(path),"%s.%d.%d.ppm",dump,state,i);if(!dump_ppm(step(0),path))goto done;}
                }
            }
            int rendered=harness(24)-before;if((harness(26)?(rendered<5||rendered>7):(rendered<samples*.9||rendered>samples*1.1))||harness(36)!=1)goto done;
            if(!step(POCKET_BTN_CROSS)||!step(0)||harness(10)!=0||harness(27)!=1)goto done;
            before=harness(24);for(int i=0;i<80;i++)if(!step(0))goto done;if(harness(27)!=1)goto done;
            printf("avatar_frames=%d native_tick_raster_mean_ms=%.3f peak_ms=%.3f samples=%d\n",rendered,total/samples,peak,samples);
            result=0;puts("PASS: bloub header portrait/morphs render, one live avatar on homepage and conversation; SR label retains Start mapping");goto done;
        }
        if(workshop_question){
            if(workshop_question==3){if(harness(9)!=1||!wait_for(3,1,10000))goto done;}
            else if(harness(1)!=1||!wait_for(3,1,10000)||harness(4)!=0||harness(2)!=1)goto done;
            if(!wait_for(60,1,10000))goto done;
            for(int i=0;i<80;i++){if(!step(0))goto done;SDL_Delay(2);}
            if(dump){char path[1400];snprintf(path,sizeof(path),"%s.question.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            if(workshop_question==2){result=0;puts("PASS: question durably waiting for reboot");goto done;}
            if(workshop_question==5){
                if(!step(POCKET_BTN_TRIANGLE)||!step(0)||!wait_for(3,1,10000)||harness(60)||harness(19))goto done;
                if(!step(POCKET_BTN_CROSS)||!step(0)||harness(10)!=0||harness(9)!=1||!wait_for(3,1,10000)||!wait_for(60,1,10000))goto done;
            }
            if(workshop_question==4){
                if(!step(POCKET_BTN_CIRCLE))goto done;
                for(int i=0;i<50;i++){if(!step(POCKET_BTN_CIRCLE))goto done;SDL_Delay(10);}
                if(harness(15)!=1||!step(0))goto done;
            }else if(!step(POCKET_BTN_DOWN)||!step(0)||harness(62)!=1||!step(POCKET_BTN_CIRCLE)||!step(0))goto done;
            if(!wait_for(4,1,30000)||!wait_for(3,1,10000)||!wait_for(60,0,10000)||!wait_for(13,1,10000)||harness(8)||harness(4)!=1)goto done;
            if(dump&&!dump_ppm(step(0),dump))goto done;
            result=0;puts("PASS: native question choice/voice/reopen resumes actual Pi and publishes same game");goto done;
        }
        if(workshop_models){
            int original=harness(23),projects=harness(16);char path[1400];
            if(harness(10)!=0||original<0||!step(POCKET_BTN_SELECT)||!step(0)||harness(10)!=2||harness(22)!=original)goto done;
            for(int i=0;i<5;i++)if(!step(POCKET_BTN_UP)||!step(0))goto done;
            if(harness(22)!=0||!step(POCKET_BTN_START)||!step(0)||!step(POCKET_BTN_TRIANGLE)||!step(0)||!step(POCKET_BTN_SQUARE)||!step(0))goto done;
            if(harness(20)||harness(19)||harness(16)!=projects||harness(23)!=original)goto done;
            if(!step(POCKET_BTN_CROSS)||!step(0)||harness(10)!=0||harness(23)!=original||!step(POCKET_BTN_SELECT)||!step(0)||harness(22)!=original)goto done;
            for(int i=0;i<5;i++)if(!step(POCKET_BTN_UP)||!step(0))goto done;
            for(int i=0;i<4;i++)if(!step(POCKET_BTN_DOWN)||!step(0))goto done;
            if(dump){snprintf(path,sizeof(path),"%s.options.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            if(harness(22)!=4||!step(POCKET_BTN_CIRCLE)||!step(0)||!wait_for(3,1,10000)||harness(8)||harness(23)!=4||harness(10)!=0)goto done;
            if(dump){snprintf(path,sizeof(path),"%s.home.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            if(!step(POCKET_BTN_SELECT)||!step(0)||harness(22)!=4||!step(POCKET_BTN_CROSS)||!step(0))goto done;
            // Reboot the real QuickJS app and restore the saved model choice.
            pocket_runtime_shutdown();
            if(!pocket_runtime_boot(js,js_size,pack,pack_size,WIDTH,HEIGHT)||!pocket_runtime_harness_bind("__workshopAcceptance")||!wait_for(0,1,10000)||harness(23)!=4||harness(10)!=0)goto done;
            if(!step(POCKET_BTN_SELECT)||!step(0)||harness(22)!=4)goto done;
            if(dump){snprintf(path,sizeof(path),"%s.reopened.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            result=0;puts("PASS: SELECT model menu, five choices, cancel without change, save, reopen/reboot persistence, isolated START/X/Y; no model/voice calls");goto done;
        }
        if(workshop_controls){
            if(harness(10)!=0||harness(16)!=4||harness(9)!=1||!wait_for(3,1,10000)||!wait_for(13,1,10000)||harness(19))goto done;
            if(!step(POCKET_BTN_RTRIGGER)||!step(0))goto done;
            for(int i=0;i<40;i++){if(!step(0))goto done;SDL_Delay(2);}
            if(harness(8)||harness(3)!=1||harness(19)||!step(POCKET_BTN_START)||!step(0)||!wait_for(20,1,10000)||!wait_for(3,1,10000))goto done;
            if(!step(POCKET_BTN_CROSS)||!step(0)||harness(10)!=0||!step(POCKET_BTN_START)||!step(0)||!wait_for(20,2,10000)||!wait_for(3,1,10000))goto done;
            if(!step(POCKET_BTN_SQUARE))goto done;
            for(int i=0;i<40;i++){if(!step(POCKET_BTN_SQUARE))goto done;SDL_Delay(10);}
            if(harness(18)<10||harness(18)>95||harness(16)!=4)goto done;
            if(dump){char path[1400];snprintf(path,sizeof(path),"%s.progress.ppm",dump);if(!dump_ppm(step(POCKET_BTN_SQUARE),path))goto done;}
            if(!step(0)||harness(18)||harness(16)!=4||harness(20)!=2)goto done;
            if(!step(POCKET_BTN_SQUARE))goto done;
            for(int i=0;i<160&&harness(16)==4;i++){if(!step(POCKET_BTN_SQUARE))goto done;SDL_Delay(10);}
            if(!step(0)||!wait_for(16,3,10000)||!wait_for(3,1,10000)||harness(8)||harness(18))goto done;
            if(dump&&!dump_ppm(step(0),dump))goto done;
            result=0;puts("PASS: START plays from both pages; R inactive; idle X hidden; Y outline progresses; short hold cancels; long hold deletes only selected project");goto done;
        }
        if(workshop_ui){
            if(harness(10)!=0 || !wait_for(14,1,10000))goto done;
            char path[1400];
            if(dump){snprintf(path,sizeof(path),"%s.list.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            if(harness(16)>=4){
                for(int i=0;i<3;i++){if(!step(POCKET_BTN_DOWN)||!step(0))goto done;}
                if(dump){snprintf(path,sizeof(path),"%s.scrolled.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
                for(int i=0;i<3;i++){if(!step(POCKET_BTN_UP)||!step(0))goto done;}
            }
            if(harness(9)!=1 || !wait_for(3,1,10000) || !wait_for(13,1,10000) || harness(10)!=1 || harness(12)<10)goto done;
            int tail=harness(11);if(!step(POCKET_BTN_UP)||!step(0)||!wait_for(13,1,10000)||harness(11)>=tail)goto done;
            if(dump){snprintf(path,sizeof(path),"%s.chat.ppm",dump);if(!dump_ppm(step(0),path))goto done;}
            if(!step(POCKET_BTN_SELECT)||!step(0)||!wait_for(13,1,10000))goto done;
            if(!step(POCKET_BTN_CIRCLE))goto done;
            for(int i=0;i<80;i++){if(!step(POCKET_BTN_CIRCLE))goto done;SDL_Delay(2);}
            if(harness(19)!=1)goto done;
            if(dump){snprintf(path,sizeof(path),"%s.voice.ppm",dump);if(!dump_ppm(step(POCKET_BTN_CIRCLE),path))goto done;}
            if(!step(POCKET_BTN_TRIANGLE)||!step(0)||harness(15))goto done;
            for(int i=0;i<80;i++){if(!step(0))goto done;SDL_Delay(2);}
            if(harness(8)||harness(19)||!step(POCKET_BTN_CROSS)||!step(0)||harness(10)!=0)goto done;
            result=0;puts("PASS: list-first startup, cover, full chat/tool history, page scrolling, details, cancelled voice never submits, B returns to list");goto done;
        }
        if(workshop_dictation){
            if(harness(10)!=0||harness(1)!=1||!wait_for(3,1,10000)||harness(4)!=0||!step(POCKET_BTN_CIRCLE))goto done;
            for(int i=0;i<120;i++){if(!step(POCKET_BTN_CIRCLE))goto done;SDL_Delay(2);}
            if(!step(0)||!wait_for(4,1,30000)||!wait_for(3,1,10000)||!wait_for(13,1,10000)||harness(8))goto done;
            const uint8_t *p=step(0);if(!p||(dump&&!dump_ppm(p,dump)))goto done;
            result=0;puts("PASS: held A, released A, final ASR text automatically submitted to the same project's real Pi tools");goto done;
        }
        if (workshop_ready) {
            if (harness(8) || !wait_for(17,1,10000)) goto done;
            const uint8_t *p=step(0); if (!p || (dump && !dump_ppm(p,dump))) goto done;
            result=0; puts("PASS: workshop initialized and rendered with thumbnails without opening display or taking input"); goto done;
        }
        if (workshop_recover) {
            if (harness(9)!=1 || !wait_for(3,1,10000) || harness(4)!=3 || harness(8)) goto done;
            const uint8_t *p=step(0); if (!p || (dump && !dump_ppm(p,dump))) goto done;
            result=0; puts("PASS: interrupted publication recovered without model request"); goto done;
        }
        if (harness(1)!=1 || !wait_for(3,1,5000) || harness(7)!=1 || harness(8)) goto done;
        /* Live requests can use six 120-second model turns; mock tests stay short. */
        double model_timeout = workshop_live ? 780000 : 30000;
        if (harness(2)!=1 || !wait_for(3,1,model_timeout) || harness(4)<1 || harness(8)) goto done;
        int first_revision = harness(4);
        if (!workshop_live && first_revision!=1) goto done;
        printf("Workshop first publication: revision=%d\n", first_revision);
        pocket_runtime_shutdown(); buttons=pressed=0; stopped=0;
        if (!pocket_runtime_boot(js,js_size,pack,pack_size,WIDTH,HEIGHT) || !pocket_runtime_harness_bind("__workshopAcceptance") || !wait_for(0,1,10000) || harness(9)!=1 || !wait_for(3,1,10000) || harness(4)!=first_revision) goto done;
        if (harness(6)!=1 || !wait_for(3,1,model_timeout) || harness(4)<=first_revision || harness(8)) goto done;
        if (!workshop_live && harness(4)!=2) goto done;
        printf("Workshop continued publication: revision=%d\n", harness(4));
        if (harness(5)!=1 || !wait_for(3,1,5000) || harness(8)) goto done;
        if (!wait_for(13,1,10000)) goto done;
        const uint8_t *p=step(0); if (!p || (dump && !dump_ppm(p,dump))) goto done;
        result=0; puts("PASS: actual Pi + PocketJS: create, publish, guest restart, continue same game, queue play"); goto done;
    }
    if (creative_bench || hardware_test) {
        if (!pocket_runtime_harness_bind("__creativeAcceptance") || !wait_for(0,1,3000)) goto done;
        if (hardware_test) {
            if(harness(16)!=1) goto done;
            SDL_Delay(1100);
            if(harness(17)!=1){fputs("FAIL: hardware feedback or telemetry\n",stderr);goto done;}
            puts("PASS: LED frame write, bounded motor request, battery and temperature interfaces");
        } else {
            const char *scenes[]={"64","256","1024","4096","3D"};
            for(int scene=0;scene<5;scene++){
                if(harness(11+scene)!=1) goto done;
                for(int j=0;j<10;j++)if(!step(0))goto done;
                double sum=0,peak=0;
                for(int j=0;j<240;j++){double t=now_ms();if(!step(0))goto done;double dt=now_ms()-t;sum+=dt;if(dt>peak)peak=dt;}
                printf("scene=%s dynamic_tick_raster_mean_ms=%.3f peak_ms=%.3f frames=240 no_sdl_present=1\n",scenes[scene],sum/240,peak);
            }
        }
        result=0;goto done;
    }
    if (creative_test) {
        if (!pocket_runtime_harness_bind("__creativeAcceptance") || !wait_for(0, 1, 3000)) goto done;
        if (harness(4) != 0 || harness(1) != 1 || harness(4) != 3 || harness(5) != 0 || harness(1) != 1) goto done;
        if (harness(2) != 1 || !wait_for(3, 1, 3000) || harness(8) != 1 || !wait_for(3, 1, 3000)) goto done;
        const uint8_t *canvas = step(0);
        if (!canvas || (dump && !dump_ppm(canvas, dump))) goto done;
        if (harness(6) != 1) goto done;
        for (int i = 0; i < 30; i++) if (!step(0)) goto done;
        if (dump) { char path[1200]; snprintf(path,sizeof(path),"%s.stress.ppm",dump); if (!dump_ppm(step(0),path)) goto done; }
        if (harness(7) != 1) goto done;
        for (int i = 0; i < 30; i++) if (!step(0)) goto done;
        if (dump) { char path[1200]; snprintf(path,sizeof(path),"%s.3d.ppm",dump); if (!dump_ppm(step(0),path)) goto done; }
        pocket_runtime_shutdown();
        if (!pocket_runtime_boot(js,js_size,pack,pack_size,WIDTH,HEIGHT) || !pocket_runtime_harness_bind("__creativeAcceptance") || !wait_for(0,1,3000) || harness(4)!=3) goto done;
        result=0; puts("PASS: creative fill, undo, save, SVG export, 1024 nodes, 3D scene and restart recovery"); goto done;
    }
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
#ifndef __APPLE__
    SDL_ShowCursor(SDL_DISABLE);
#endif
    SDL_Window *window = SDL_CreateWindow("PocketJS Brick App", 0, 0, WIDTH, HEIGHT, SDL_WINDOW_SHOWN |
#ifdef __APPLE__
        SDL_WINDOW_RESIZABLE
#else
        SDL_WINDOW_FULLSCREEN_DESKTOP
#endif
    );
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
        const uint8_t *p = step(frame_buttons());
        if (!p || SDL_UpdateTexture(texture, NULL, p, WIDTH * 4) || SDL_RenderClear(renderer) || SDL_RenderCopy(renderer, texture, NULL, NULL)) goto sdl_done;
        double before_present = now_ms(); SDL_RenderPresent(renderer);
        brick_hardware_frame(before_present - start, now_ms() - before_present);brick_services_frame(before_present-start); frames++;
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
    printf("guest_tick_raster_max_ms=%.3f peak_rss_kib=%ld exit=%d\n", max_work_ms,
#ifdef __APPLE__
        usage.ru_maxrss / 1024,
#else
        usage.ru_maxrss,
#endif
        result);
    return result;
}
