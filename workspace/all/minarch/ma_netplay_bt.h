#pragma once
#include <stdbool.h>
#include <stddef.h>

#define NETPLAY_BT_ROOMS 8
struct NetplayBTRoom { char address[18], label[64]; signed char rssi; };
struct NetplayBTEvent { char kind, text[160]; int fd; };
/* One private child per connection or scan. All operations are nonblocking. */
bool Netplay_btStart(bool host, const char *peer);
bool Netplay_btScan(void);
void Netplay_btStop(void);
/* Pre-game identity rejection only: bounded asynchronous queued-byte drain. */
void Netplay_btDrainStop(void);
void Netplay_btReap(void);
bool Netplay_btEvent(struct NetplayBTEvent *event);
bool Netplay_btScanning(void);
bool Netplay_btScanFailed(void);
unsigned Netplay_btRooms(struct NetplayBTRoom rooms[NETPLAY_BT_ROOMS]);
const char *Netplay_btScanStatus(void);
