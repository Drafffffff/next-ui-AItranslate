/* Exercise the actual patched gpSP adapter with ordered Wi-Fi bursts.
 * Only the external CPU registers, RNG and network callbacks are fixtures. */
#include <assert.h>
#include <stdio.h>
#include RFU_SOURCE

u32 cpu_ticks;
u16 io_registers[512];
void rand_seed(u32 seed) { (void)seed; }
u16 rand_gen(void) { return 1234; }
static unsigned acknowledgements;
void netpacket_send(uint16_t id, const void *buf, size_t len) {
  (void)id; (void)buf; (void)len; acknowledgements++;
}
void netpacket_poll_receive(void) {}

static void burst(bool host, unsigned count) {
  rfu_reset();
  acknowledgements = 0;
  rfu_state = host ? RFU_STATE_HOST : RFU_STATE_CLIENT;
  rfu_client.devid = rfu_host.clients[0].devid = 42;
  rfu_client.clnum = 0;
  for (u32 i = 1; i <= count; i++) {
    u32 packet[4] = {netorder32(NET_RFU_HEADER),
      netorder32(host ? NET_RFU_CLIENT_SEND : NET_RFU_HOST_SEND),
      netorder32(host ? (4U << 24) | 42 : 4), i};
    rfu_net_receive(packet, sizeof(packet), 0);
  }
  unsigned expected = MIN(count, RFU_RX_QUEUE);
  assert((host ? rfu_queue_stats.host_peak : rfu_queue_stats.client_peak) == expected);
  assert((host ? rfu_queue_stats.host_dropped : rfu_queue_stats.client_dropped) == count - expected);
  if (!host) assert(acknowledgements == count);
  for (u32 i = 1; i <= expected; i++) {
    rfu_cmd = RFU_CMD_RECV_DATA;
    assert(rfu_process_command() == 2);
    assert(rfu_buf[0] == (host ? 4U << 8 : 4));
    assert(rfu_buf[1] == i);
  }
  rfu_cmd = RFU_CMD_RECV_DATA;
  assert(rfu_process_command() == 1 && rfu_buf[0] == 0);
  assert(!rfu_data_avail());
  printf("%s: %u burst packets, %u ordered deliveries, %u bounded overflow drops\n",
         host ? "host" : "client", count, expected, count - expected);
}
int main(void) {
  burst(false, 8);
  burst(true, 8);
  burst(false, RFU_RX_QUEUE);
  burst(true, RFU_RX_QUEUE);
  burst(false, RFU_RX_QUEUE + 1);
  burst(true, RFU_RX_QUEUE + 1);
  rfu_reset();
  assert(rfu_queue_stats.host_peak == 0 && rfu_queue_stats.client_peak == 0);
  assert(rfu_queue_stats.host_dropped == 0 && rfu_queue_stats.client_dropped == 0);
  puts("PASS: actual RFU queues preserve both directions, drain in order, bound overflow, reset cleanly");
  return 0;
}
