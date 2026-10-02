#!/usr/bin/env python3
"""Exercise the real power policy with a monotonic clock and mocked kernel suspend.

No hardware state, Bluetooth service, or user file is modified.
"""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
POWER = ROOT / 'ports/brick-mic/brick/mic-power.c'

SDL = r'''
#ifndef MIC_TEST_SDL_H
#define MIC_TEST_SDL_H
#include <stdint.h>
typedef uint32_t Uint32;
#define SDL_KEYDOWN 1
#define SDL_KEYUP 2
#define SDL_JOYBUTTONDOWN 3
#define SDL_JOYBUTTONUP 4
Uint32 SDL_GetTicks(void);
void SDL_PumpEvents(void);
int SDL_HasEvents(Uint32 min,Uint32 max);
#endif
'''

HARNESS = r'''
#define BRICK_MIC_POWER_TEST
#include <assert.h>
#include <stdlib.h>
#include "POWER_SOURCE"
static Uint32 tick;
static int connection_state,prepares,aborts,suspends,last_asleep,last_restart,prepare_ok,suspend_rc,pending,prepare_wakes;
Uint32 SDL_GetTicks(void){return tick;}
void SDL_PumpEvents(void){}
int SDL_HasEvents(Uint32 a,Uint32 b){(void)a;(void)b;return pending;}
void brick_mic_sleep(int asleep,int restart){last_asleep=asleep;last_restart=restart;}
int brick_mic_connection(void){return connection_state;}
int brick_mic_prepare_deep_sleep(void){prepares++;if(prepare_wakes)mic_power_toggle();return prepare_ok;}
void brick_mic_abort_deep_sleep(void){aborts++;}
int brick_mic_test_platform_suspend(void){suspends++;return suspend_rc;}
static void reset(void){
    tick=100;connection_state=1;prepares=aborts=suspends=last_asleep=last_restart=pending=prepare_wakes=0;prepare_ok=1;suspend_rc=0;
    setenv("BRICK_MIC_PREVIEW","1",1);mic_power_init();screen_timeout=0;
}
static void step(Uint32 ms){tick+=ms;mic_power_poll();}
static void disconnected(void){connection_state=0;mic_power_poll();}
int main(void){
    reset();connection_state=0;step(3600000);assert(suspends==0&&!mic_power_sleeping());
    puts("PASS: no automatic deep sleep while the screen is on");
    reset();mic_power_toggle();step(600000);assert(suspends==0&&mic_power_sleeping());
    puts("PASS: connected screen-off standby retains Bluetooth indefinitely");
    reset();mic_power_toggle();disconnected();step(299999);assert(suspends==0);step(1);
    assert(suspends==1&&!mic_power_sleeping()&&last_asleep==0&&last_restart==1);
    assert(mic_power_take_deep_wake()==1&&mic_power_take_deep_wake()==0);
    puts("PASS: five minutes of disconnection enters platform suspend, then uses deep resume");
    reset();mic_power_toggle();disconnected();step(240000);connection_state=1;step(1);disconnected();step(299999);assert(suspends==0);step(1);assert(suspends==1);
    puts("PASS: reconnection resets the continuous disconnect timer");
    reset();mic_power_toggle();disconnected();step(240000);connection_state=-1;step(1);disconnected();step(299999);assert(suspends==0);step(1);assert(suspends==1);
    puts("PASS: stale connection status cannot trigger deep sleep");
    reset();mic_power_toggle();disconnected();prepare_ok=0;step(300000);assert(prepares==1&&suspends==0&&mic_power_sleeping());step(1);assert(prepares==1);
    puts("PASS: daemon reconnect reservation rejects deep sleep without a busy retry loop");
    reset();mic_power_toggle();disconnected();pending=1;step(300000);assert(prepares==1&&aborts==1&&suspends==0&&mic_power_sleeping());
    puts("PASS: a pending user key cancels the final suspend preparation");
    reset();mic_power_toggle();disconnected();prepare_wakes=1;step(300000);assert(aborts==1&&suspends==0&&!mic_power_sleeping());
    puts("PASS: wake during preparation wins over deep sleep");
    reset();mic_power_toggle();disconnected();step(240000);mic_power_toggle();step(600000);assert(suspends==0&&!mic_power_sleeping());
    puts("PASS: screen wake cancels the countdown");
    reset();mic_power_toggle();disconnected();suspend_rc=-1;step(300000);assert(suspends==1&&aborts==1&&!mic_power_sleeping()&&last_restart==1);
    puts("PASS: failed platform suspend restores UI and reconnects services");
    reset();tick=0xffffff00u;mic_power_toggle();disconnected();step(299999);assert(suspends==0);step(1);assert(suspends==1);
    puts("PASS: countdown remains correct across SDL clock wrap");
    reset();mic_power_toggle();disconnected();step(299999);mic_power_button(1);step(1);assert(suspends==0);step(999);assert(mic_power_shutdown_requested()&&suspends==0);
    puts("PASS: power-button shutdown has priority over automatic deep sleep");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='brick-mic-sleep-check-') as folder:
    root = Path(folder)
    (root / 'SDL.h').write_text(SDL)
    (root / 'check.c').write_text(HARNESS.replace('POWER_SOURCE', str(POWER)))
    compiler = os.environ.get('CC', 'clang')
    subprocess.run([compiler, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-D__APPLE__', '-I' + str(root), str(root / 'check.c'),
                    '-o', str(root / 'check')], check=True)
    subprocess.run([str(root / 'check')], check=True, timeout=10)
