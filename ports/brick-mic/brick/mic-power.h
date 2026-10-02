#ifndef BRICK_MIC_POWER_H
#define BRICK_MIC_POWER_H
void mic_power_init(void);
void mic_power_presented(void);
void mic_power_poll(void);
void mic_power_button(int down);
void mic_power_activity(void);
void mic_power_toggle(void);
int mic_power_sleeping(void);
int mic_power_fast_wake(void);
/* Consume a completed platform suspend/resume cycle to clear old input edges. */
int mic_power_take_deep_wake(void);
int mic_power_shutdown_requested(void);
void mic_power_quit(int preserve);
#endif
