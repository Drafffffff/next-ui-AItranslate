#include <errno.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop(int signal_number) { stopping = signal_number; }

int main(int argc, char **argv) {
    if (argc < 2) return 64;
    int input = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
    struct sigaction action = {0};
    action.sa_handler = stop;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);
    pid_t child = fork();
    if (child < 0) return 71;
    if (child == 0) {
        if (input >= 0) close(input);
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGUSR1, SIG_DFL);
        execv(argv[1], argv + 1);
        perror("execv");
        _exit(127);
    }
    int status = 0, ticks = 0;
    int sent = 0;
    for (;;) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) break;
        if (result < 0 && errno != EINTR) return 71;
        struct js_event event;
        while (input >= 0 && read(input, &event, sizeof(event)) == sizeof(event)) {
            /* Ignore initial state: MENU may still be held while launching. */
            if (event.type == JS_EVENT_BUTTON && event.number == 8 && event.value)
                stopping = SIGUSR1;
        }
        if (stopping && !sent) {
            fprintf(stderr, "MENU exit: requesting app shutdown (signal %d)\n", (int)stopping);
            kill(child, stopping);
            sent = 1;
        }
        if (sent && ++ticks == 100) {
            fprintf(stderr, "MENU exit: app unresponsive, sending SIGTERM\n");
            kill(child, SIGTERM);
        }
        if (sent && ticks == 200) {
            fprintf(stderr, "MENU exit: app unresponsive, sending SIGKILL\n");
            kill(child, SIGKILL);
        }
        struct timespec delay = {0, 20000000};
        nanosleep(&delay, NULL);
    }
    if (input >= 0) close(input);
    if (stopping) fprintf(stderr, "MENU exit: child status %d\n", status);
    return stopping ? 0 : (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
}
