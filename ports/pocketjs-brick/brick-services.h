#ifndef BRICK_SERVICES_H
#define BRICK_SERVICES_H
#include <stddef.h>
/* UI-thread entry points for diagnostics only; jobs never touch QuickJS/core. */
int brick_services_pending(void);
void brick_services_frame(double ms);
#endif
