/* Actual RFU adapter tests. CPU registers, RNG and network callbacks are fixtures. */
#include <assert.h>
#include <stdio.h>
#include RFU_SOURCE

u32 cpu_ticks;
u16 io_registers[512];
void rand_seed(u32 seed) { (void)seed; }
u16 rand_gen(void) { return 1234; }
static unsigned sends, acknowledgements, uplinks;
static u32 last_payload, last_type;
void netpacket_send(uint16_t id, const void *buf, size_t len) {
  (void)id;
  const u8 *bytes = buf;
  assert(len >= 12);
  sends++;
  last_type = upack32(bytes + 4);
  if (last_type == NET_RFU_CLIENT_ACK) acknowledgements++;
  if (last_type == NET_RFU_CLIENT_SEND) {
    uplinks++;
    last_payload = len >= 16 ? leupack32(bytes + 12) : 0;
  }
}
void netpacket_poll_receive(void) {}
static void setup(bool host) {
  rfu_reset();
  sends = acknowledgements = uplinks = 0;
  rfu_state = host ? RFU_STATE_HOST : RFU_STATE_CLIENT;
  rfu_client.devid = rfu_host.clients[0].devid = 42;
  rfu_client.clnum = rfu_client.host_id = rfu_host.clients[0].client_id = 0;
}
static void incoming(bool host, unsigned value) {
  u32 packet[4] = {netorder32(NET_RFU_HEADER),
    netorder32(host ? NET_RFU_CLIENT_SEND : NET_RFU_HOST_SEND),
    netorder32(host ? (4U << 24) | 42 : 4), value};
  rfu_net_receive(packet, sizeof(packet), 0);
}
static void mailbox(bool host) {
  setup(host);
  for (unsigned i = 1; i <= 100; i++) incoming(host, i);
  assert((host ? rfu_queue_stats.host_replaced : rfu_queue_stats.client_replaced) == 99);
  assert(!rfu_queue_stats.host_dropped && !rfu_queue_stats.client_dropped);
  if (!host) assert(acknowledgements == 100);
  rfu_cmd = RFU_CMD_RECV_DATA;
  assert(rfu_process_command() == 2 && rfu_buf[1] == 100);
  assert(rfu_process_command() == 1 && rfu_buf[0] == 0);
  puts(host ? "PASS: host mailbox exposes latest exchange, clears on read" :
              "PASS: child mailbox exposes latest exchange, clears on read");
}
static void stage(unsigned value) {
  rfu_cmd = RFU_CMD_SEND_DATAW; rfu_plen = 2;
  rfu_buf[0] = 4U << 8; rfu_buf[1] = value;
  assert(rfu_process_command() == 0);
}
static void host_wait(void) {
  rfu_cmd = RFU_CMD_SEND_DATAW; rfu_plen = 2;
  rfu_buf[0] = 4; rfu_buf[1] = 99;
  assert(rfu_process_command() == 0);
  rfu_plen = 0; // process_command's return replaces the command parameter count.
  rfu_comstate = RFU_COMSTATE_RESPCMD;
  assert(rfu_transfer(0x80000000) == 0x996600a5);
  write_ioreg(REG_SIOCNT, 0); // Wait armed, but don't consume response yet.
}
int main(void) {
  mailbox(false); mailbox(true);
  setup(false);
  stage(10); stage(20);
  assert(sends == 0 && rfu_client.tx_pending);
  incoming(false, 1);
  assert(uplinks == 1 && last_payload == 20 && acknowledgements == 1);
  incoming(false, 2);
  assert(uplinks == 1 && acknowledgements == 2);
  puts("PASS: child stages latest uplink; only host clocks it out, once");

  setup(true); host_wait();
  incoming(true, 55); // Payload must not double as transport ACK.
  assert(rfu_host.clients[0].acked == 0);
  rfu_update(1);
  assert(rfu_comstate == RFU_COMSTATE_WAITEVENT);
  u32 ack[4] = {netorder32(NET_RFU_HEADER), netorder32(NET_RFU_CLIENT_ACK), netorder32(42), 0};
  rfu_net_receive(ack, sizeof(ack), 1); // Wrong peer cannot ACK.
  assert(rfu_host.clients[0].acked == 0);
  rfu_net_receive(ack, sizeof(ack), 0);
  rfu_update(1);
  assert(rfu_comstate == RFU_COMSTATE_WAITRESP && rfu_buf[0] == 0x99660028);
  assert(rfu_queue_stats.host_no_reply == 0);
  puts("PASS: valid ACK completes host wait without fabricated error");

  setup(true); host_wait(); rfu_update(16777216);
  assert(rfu_buf[0] == 0x99660128 && rfu_buf[1] == 0);
  assert(rfu_queue_stats.host_no_reply == 1); // Missing peer not called inactive.
  puts("PASS: missing response reports actual receiver mask, no phantom slots");

  setup(true); rfu_timeout = rfu_rtx_max = 0; host_wait();
  rfu_update(16777216);
  assert(rfu_comstate == RFU_COMSTATE_WAITEVENT);
  puts("PASS: zero timeout/retry configuration means unlimited wait");

  setup(false); rfu_comstate = RFU_COMSTATE_WAITEVENT;
  rfu_timeout_cycles = 100; write_ioreg(REG_SIOCNT, 0);
  u32 empty[4] = {netorder32(NET_RFU_HEADER), netorder32(NET_RFU_HOST_SEND), 0, 0};
  rfu_net_receive(empty, sizeof(empty), 0); rfu_update(1);
  assert(rfu_comstate == RFU_COMSTATE_WAITRESP && !rfu_client.rx_event);
  puts("PASS: zero-byte host exchange wakes child without stale payload");
  rfu_reset();
  assert(!rfu_client.tx_pending && !rfu_client.rx_event);
  assert(!rfu_queue_stats.host_replaced && !rfu_queue_stats.client_replaced);
  puts("PASS: reset clears mailbox and exchange state");
  return 0;
}
