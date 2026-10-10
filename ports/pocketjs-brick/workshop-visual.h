#ifndef WORKSHOP_VISUAL_H
#define WORKSHOP_VISUAL_H
#include "quickjs.h"
int workshop_visual_boot(JSContext *ctx,JSValue info);
void workshop_visual_shutdown(void);
int workshop_cover_prepare(const char *id,char **output,size_t *length);
#endif
