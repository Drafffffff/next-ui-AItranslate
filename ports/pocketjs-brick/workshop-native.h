#ifndef WORKSHOP_NATIVE_H
#define WORKSHOP_NATIVE_H
#include "quickjs.h"
#include <stddef.h>
int workshop_boot(JSContext *ctx, JSValue bridge);
void workshop_configure_font(void);
int workshop_allowed_file(const char *name);
int workshop_job(const char *op, const char *id, const char *text, const char *title,
                 char **output, size_t *length, char *error, size_t error_size);
#endif
