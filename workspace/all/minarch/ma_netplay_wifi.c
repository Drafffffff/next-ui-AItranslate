#define _POSIX_C_SOURCE 200809L
#include "ma_netplay_wifi.h"
#ifdef NEXTUI_LINK_WIFI
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static bool restore_power_save;
static unsigned long long clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
/* Fixed executable/arguments, bounded lifetime; no shell or player-supplied text. */
static bool wifi_command(bool query, bool enabled, char output[128]) {
    int pipes[2];
    if (pipe(pipes)) return false;
    pid_t child = fork();
    if (child < 0) { close(pipes[0]); close(pipes[1]); return false; }
    if (!child) {
        int null = open("/dev/null", O_RDWR);
        if (null < 0 || dup2(null, STDIN_FILENO) < 0 ||
            dup2(null, STDERR_FILENO) < 0 || dup2(pipes[1], STDOUT_FILENO) < 0) _exit(127);
        close(null); close(pipes[0]); close(pipes[1]);
        if (query) execl("/usr/sbin/iw", "iw", "dev", "wlan0", "get", "power_save", (char *)NULL);
        else execl("/usr/sbin/iw", "iw", "dev", "wlan0", "set", "power_save", enabled ? "on" : "off", (char *)NULL);
        _exit(127);
    }
    close(pipes[1]);
    fcntl(pipes[0], F_SETFL, O_NONBLOCK);
    output[0] = '\0';
    size_t used = 0;
    int result = 0;
    unsigned long long deadline = clock_ms() + 1000;
    bool finished = false;
    while (clock_ms() < deadline) {
        char discard[128];
        ssize_t n = read(pipes[0], used < 127 ? output + used : discard, used < 127 ? 127 - used : sizeof(discard));
        if (n > 0 && used < 127) { used += (size_t)n; output[used] = '\0'; }
        pid_t waited = waitpid(child, &result, WNOHANG);
        if (waited == child) {
            /* The child may have exited just after the read above. Drain its final line. */
            while (used < 127 && (n = read(pipes[0], output + used, 127 - used)) > 0) used += (size_t)n;
            output[used] = '\0';
            finished = true;
            break;
        }
        if (waited < 0 && errno != EINTR) break;
        struct pollfd p = {.fd=pipes[0], .events=POLLIN};
        poll(&p, 1, 10);
    }
    if (!finished) {
        kill(child, SIGKILL);
        while (waitpid(child, &result, 0) < 0 && errno == EINTR) {}
    }
    close(pipes[0]);
    return finished && WIFEXITED(result) && WEXITSTATUS(result) == 0;
}
void Netplay_wifiBegin(void) {
    char output[128];
    if (!wifi_command(true, false, output)) {
        fprintf(stderr, "GBA Link Wi-Fi: unable to read power saving; leaving it unchanged\n");
        return;
    }
    if (strstr(output, "Power save: on")) {
        restore_power_save = wifi_command(false, false, output);
        fprintf(stderr, "GBA Link Wi-Fi: power saving %s\n", restore_power_save ? "disabled for this session" : "could not be disabled");
    } else fprintf(stderr, "GBA Link Wi-Fi: power saving already off\n");
}
void Netplay_wifiEnd(void) {
    if (restore_power_save) {
        char output[128];
        if (wifi_command(false, true, output)) {
            restore_power_save = false;
            fprintf(stderr, "GBA Link Wi-Fi: original power saving restored\n");
        } else fprintf(stderr, "GBA Link Wi-Fi: restore failed; will retry when the game closes\n");
    }
}
#else
void Netplay_wifiBegin(void) {}
void Netplay_wifiEnd(void) {}
#endif
