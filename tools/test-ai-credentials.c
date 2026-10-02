#define _POSIX_C_SOURCE 200809L
#include "../workspace/all/common/ai_credentials.h"
#include <assert.h>
#include <unistd.h>

static char path[] = "/tmp/nextui-ai-keys-XXXXXX";
static void fixture(const char *text, size_t size) {
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(text,1,size,f)==size); assert(!fclose(f));
}
static void check(const char *text, int result, const char *expected) {
    fixture(text,strlen(text));
    char key[512];
    assert(nextui_ai_read_key("deepseek",key,sizeof(key))==result);
    assert(!strcmp(key,expected));
}
int main(void) {
    int fd=mkstemp(path); assert(fd>=0); close(fd);
    assert(!setenv("NEXTUI_AI_KEYS_FILE",path,1));
    check("\xef\xbb\xbf# config\r\nDEEPSEEK_API_KEY = mock-shared\r\nDASHSCOPE_API_KEY=mock-asr\n",1,"mock-shared");
    char key[512]; assert(nextui_ai_read_key("bailian",key,sizeof(key))==1 && !strcmp(key,"mock-asr"));
    check("DEEPSEEK_API_KEY=changed\n",1,"changed");
    check("DEEPSEEK_API_KEY=\n",0,"");
    check("CUSTOM_API_KEY=unused\n",0,"");
    check("DEEPSEEK_API_KEY=one\nDEEPSEEK_API_KEY=two\n",-1,"");
    check("DEEPSEEK_API_KEY=contains spaces\n",-1,"");
    check("DEEPSEEK_API_KEY='$(never-executed)'\\literal\n",1,"'$(never-executed)'\\literal");
    const char nul[]="DEEPSEEK_API_KEY=ab\0cd"; fixture(nul,sizeof(nul)-1);
    assert(nextui_ai_read_key("deepseek",key,sizeof(key))==-1 && !key[0]);
    char large[16385];memset(large,'x',sizeof(large));fixture(large,sizeof(large));
    assert(nextui_ai_read_key("deepseek",key,sizeof(key))==-1);
    assert(!strcmp(nextui_ai_provider_for_url("https://api.deepseek.com/chat/completions"),"deepseek"));
    assert(!strcmp(nextui_ai_provider_for_url("https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"),"bailian"));
    assert(!nextui_ai_provider_for_url("https://api.deepseek.com.evil.test/"));
    assert(!nextui_ai_provider_for_url("https://api.deepseek.com@evil.test/"));
    assert(!nextui_ai_provider_for_url("http://127.0.0.1:8080/"));
    unlink(path);assert(nextui_ai_read_key("deepseek",key,sizeof(key))==0);
    puts("PASS: provider isolation, shared reload, legacy fallback signal, literal parsing, invalid config and bounds");
}
