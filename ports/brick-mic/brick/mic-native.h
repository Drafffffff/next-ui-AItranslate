#ifndef BRICK_MIC_NATIVE_H
#define BRICK_MIC_NATIVE_H
void brick_mic_first_present(void);
void brick_mic_sleep(int asleep,int restart);
/* 1 = connected, 0 = fresh disconnected status, -1 = unavailable/stale. */
int brick_mic_connection(void);
int brick_mic_prepare_deep_sleep(void);
void brick_mic_abort_deep_sleep(void);
#endif
