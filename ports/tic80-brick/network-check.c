#include "net.h"
#include <stdio.h>
#include <unistd.h>

static int complete, success;
static void response(const net_get_data *data) {
    if (data->type == net_get_progress) return;
    complete = 1;
    if (data->type == net_get_done && data->done.size > 100) {
        printf("PASS: %s (%d bytes)\n", data->url, data->done.size);
        success = 1;
    } else {
        fprintf(stderr, "FAIL: %s (code %d)\n", data->url,
            data->type == net_get_error ? data->error.code : 0);
    }
}
int main(void) {
    tic_net *net = tic_net_create("https://tic80.com");
    if (!net) return 1;
    const char *paths[] = {
        "/json?fn=dir&path=",
        "/cart/b88b74e7a6f923251de764d89d6f3507/8_bit_panda.tic"
    };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(*paths); ++i) {
        complete = success = 0;
        tic_net_get(net, paths[i], response, NULL);
        for (int tick = 0; tick < 1500 && !complete; ++tick) {
            tic_net_end(net);
            usleep(20000);
        }
        if (!success) { tic_net_close(net); return 1; }
    }
    tic_net_close(net);
    return 0;
}
