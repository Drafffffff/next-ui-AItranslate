#ifndef BRICK_SERVICES_H
#define BRICK_SERVICES_H
#include <stddef.h>
#include <stdint.h>
typedef struct JSContext JSContext;
int32_t brick_services_boot(JSContext *ctx, const uint8_t *pak, size_t size, int32_t w, int32_t h);
void brick_services_shutdown(int32_t gl);
/* UI-thread entry points for diagnostics only; jobs never touch QuickJS/core. */
int brick_services_pending(void);
#endif
