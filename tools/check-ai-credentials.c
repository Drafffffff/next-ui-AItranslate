#include "../workspace/all/common/ai_credentials.h"

/* On-device diagnostic: reports presence only, never credentials. */
int main(void) {
    const char *providers[] = {"deepseek", "bailian", "custom"};
    int invalid = 0;
    for (unsigned i = 0; i < sizeof(providers)/sizeof(providers[0]); i++) {
        char key[512];
        int status = nextui_ai_read_key(providers[i], key, sizeof(key));
        printf("%s: %s\n", providers[i], status > 0 ? "configured" : status == 0 ? "empty/missing" : "invalid");
        if (status < 0) invalid = 1;
    }
    return invalid;
}
