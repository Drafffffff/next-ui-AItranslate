#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "libretro.h"

/* Two consoles, two independent games/saves; only emulated link packets travel.
 * All callbacks run on the emulation thread. Wire protocol is NextUI-specific,
 * not RetroArch netplay. TCP provides reliable, ordered packet delivery. */
enum NPState { NP_IDLE, NP_LISTENING, NP_CONNECTING, NP_HANDSHAKE, NP_ACTIVE, NP_ERROR };
bool Netplay_setInterface(const struct retro_netpacket_callback *iface);
bool Netplay_supported(void);
void Netplay_identity(const uint8_t rom_sha256[32], const uint8_t core_sha256[32], const char *serial_mode);
bool Netplay_host(uint16_t port);
bool Netplay_join(const char *ipv4, uint16_t port);
bool Netplay_relay(const char *ipv4, uint16_t port, const char *room, bool host);
void Netplay_poll(void);
void Netplay_wait(unsigned microseconds); // existing pacing wait, with socket wakeups
void Netplay_stop(const char *reason);
void Netplay_pause(bool paused);
bool Netplay_active(void);
bool Netplay_busy(void);
bool Netplay_paused(void);
bool Netplay_localPaused(void);
bool Netplay_used(void);
enum NPState Netplay_state(void);
const char *Netplay_status(void);
uint64_t Netplay_sent(void);
uint64_t Netplay_received(void);
void Netplay_clear(void);
