#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "../workspace/all/nextui/pinyin.h"
static void check(const char *name, const char *expected, int initial) {
    char *key=Pinyin_key(name);
    if (!key || strcmp(key,expected)) fprintf(stderr,"%s: got %s, expected %s\n",name,key ? key : "NULL",expected);
    assert(key && !strcmp(key,expected));
    assert(Pinyin_initial(key)==initial);
    free(key);
}
int main(void) {
    check("超级马里奥", "chaojimaliao",3);
    check("塞尔达传说", "saierdachuanshuo",19);
    check("重装机兵", "zhongzhuangjibing",26);
    check("音乐", "yinyue",25);
    check("快樂", "kuaile",11);
    check("最终幻想", "zuizhonghuanxiang",26);
    check("ABC中文123", "abczhongwen123",1);
    check("123游戏", "123youxi",0);
    check("", "",0);
    check("\xf0\x9f\x8e\xae", "\xf0\x9f\x8e\xae",0);
    check("\xe4\xb8", "\xe4\xb8",0);
    char *a=Pinyin_key("超级"), *b=Pinyin_key("魂斗罗"), *c=Pinyin_key("最终");
    assert(strcmp(a,b)<0 && strcmp(b,c)<0);free(a);free(b);free(c);
    /* Allocation bound and malformed byte sequences under ASan. */
    for (int i=1;i<256;i++) { char s[]={i,0}; char *k=Pinyin_key(s); assert(k); free(k); }
    clock_t start=clock();
    for (int i=0;i<10000;i++) { char *key=Pinyin_key("重装机兵2：超级马里奥与最终幻想 (中文版)");free(key); }
    printf("PASS: 拼音、繁体、多音词、中英混排、首字母与损坏UTF-8；10000个排序键 %.1f ms (host)\n",1000.0*(clock()-start)/CLOCKS_PER_SEC);
}
