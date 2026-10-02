#ifndef BRICK_HARDWARE_H
#define BRICK_HARDWARE_H
#include <stdint.h>
#include "quickjs.h"
int brick_hardware_boot(JSContext *ctx, JSValue bridge);
void brick_hardware_shutdown(void);
const uint8_t *brick_canvas_render(const uint8_t *source);
void brick_hardware_frame(double work_ms, double present_ms);
#endif
