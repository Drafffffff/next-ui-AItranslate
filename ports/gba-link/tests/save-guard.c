/* Exercise the actual ma_saves.c: fake only the libretro core and UI services.
 * Files, marker persistence, resume dispatch and replacement code are real. */
#include "ma_internal.h"
#include "ma_saves.h"
#include "ma_netplay.h"
#include "notification.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct Core core;
struct Game game;
int fast_forward;
static bool busy, used, fail_serialize;
static unsigned current, restored;
bool Netplay_busy(void) { return busy; }
bool Netplay_used(void) { return used; }
bool RA_isHardcoreModeActive(void) { return false; }
void Rewind_on_state_change(void) {}
int CFG_getStateFormat(void) { return STATE_FORMAT_SAV; }
void Notification_push(NotificationType type,const char *message,SDL_Surface *icon) {
    (void)type;(void)icon;puts(message);
}
void LOG_note(int level,const char *format,...) {(void)level;(void)format;}
int exists(char *path) { return access(path,F_OK)==0; }
int getInt(char *path) { FILE *f=fopen(path,"r");int value=0;assert(f);assert(fscanf(f,"%d",&value)==1);fclose(f);return value; }
static size_t state_size(void) { return 32; }
static bool serialize(void *data,size_t size) {
    assert(size==32);if(fail_serialize)return false;memset(data,0,32);memcpy(data,&current,sizeof(current));return true;
}
static bool unserialize(const void *data,size_t size) {
    assert(size==32);memcpy(&current,data,sizeof(current));restored++;return true;
}
static void resume_slot(int slot) { FILE *f=fopen(RESUME_SLOT_PATH,"w");assert(f);fprintf(f,"%d",slot);fclose(f); }
int main(void) {
    char directory[]="/tmp/nextui-save-guard-XXXXXX";assert(mkdtemp(directory));
    snprintf((char*)core.states_dir,sizeof(core.states_dir),"%s",directory);
    strcpy(game.alt_name,"test.gba");core.serialize_size=state_size;core.serialize=serialize;core.unserialize=unserialize;
    state_slot=AUTO_RESUME_SLOT;current=11;assert(State_write());current=22;assert(State_read());assert(current==11);
    assert(State_prepareLink());busy=true;current=22;assert(!State_write());assert(!State_read());assert(current==22);
    busy=false;used=true;assert(!State_read());assert(!State_write());
    used=false;current=33;unsigned count=restored;resume_slot(AUTO_RESUME_SLOT);State_resume();
    assert(current==33 && restored==count);assert(!exists(RESUME_SLOT_PATH));
    fail_serialize=true;assert(!State_write());fail_serialize=false;resume_slot(AUTO_RESUME_SLOT);State_resume();
    assert(current==33 && restored==count); /* failed write must not release the old-resume quarantine */
    assert(State_write());current=44;resume_slot(AUTO_RESUME_SLOT);State_resume();assert(current==33 && restored==count+1);
    state_slot=0;current=55;assert(State_write());assert(State_prepareLink());current=66;resume_slot(0);State_resume();assert(current==55);
    printf("PASS: active/ended link blocks states; fresh reopen skips stale auto state; failed write keeps quarantine; successful solo auto save releases it; explicit manual load remains allowed.\n");
    return 0;
}
