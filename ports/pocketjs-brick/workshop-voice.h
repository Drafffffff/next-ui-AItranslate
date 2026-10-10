#ifndef WORKSHOP_VOICE_H
#define WORKSHOP_VOICE_H
#include "quickjs.h"
int workshop_voice_boot(JSContext *ctx, JSValue info);
void workshop_voice_shutdown(void);
#endif
