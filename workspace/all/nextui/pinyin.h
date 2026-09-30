#ifndef NEXTUI_PINYIN_H
#define NEXTUI_PINYIN_H
#include <stdlib.h>
#include <string.h>
#include "pinyin_data.h"

/* Invalid/unknown UTF-8 stays byte-for-byte intact; never use locale-sensitive
 * ctype on signed bytes. ASCII and Chinese share lowercase Latin sort keys. */
static unsigned int Pinyin_decode(const unsigned char *s, size_t *bytes) {
    unsigned int cp = s[0];
    int n = cp >= 0xf0 && cp <= 0xf4 ? 4 : cp >= 0xe0 && cp <= 0xef ? 3 : cp >= 0xc2 && cp <= 0xdf ? 2 : 1;
    *bytes = 1;
    if (n == 1) return cp;
    cp &= (1u << (7-n))-1;
    for (int i=1; i<n; i++) {
        if (!s[i] || (s[i]&0xc0)!=0x80) return s[0];
        cp = (cp<<6) | (s[i]&0x3f);
    }
    if ((n==2 && cp<0x80) || (n==3 && cp<0x800) || (n==4 && cp<0x10000)
        || (cp>=0xd800 && cp<=0xdfff) || cp>0x10ffff) return s[0];
    *bytes = n;
    return cp;
}
static const char *Pinyin_char(unsigned int cp) {
    size_t lo=0, hi=sizeof(pinyin_chars)/sizeof(pinyin_chars[0]);
    while (lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        if (pinyin_chars[mid].cp<cp) lo=mid+1;
        else if (pinyin_chars[mid].cp>cp) hi=mid;
        else return pinyin_chars[mid].syllable;
    }
    return NULL;
}
static const char *Pinyin_phrase(const char *s, size_t bytes) {
    size_t lo=0, hi=sizeof(pinyin_phrases)/sizeof(pinyin_phrases[0]);
    while (lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        const char *word=pinyin_phrases[mid].word;
        int cmp=strncmp(s,word,bytes);
        if (!cmp && strlen(word)>bytes) cmp=-1;
        if (cmp>0) lo=mid+1;
        else if (cmp<0) hi=mid;
        else return pinyin_phrases[mid].key;
    }
    return NULL;
}
static char *Pinyin_key(const char *name) {
    size_t len=strlen(name);
    if (len > ((size_t)-1-1)/3) return NULL;
    char *key=malloc(len*3+1), *out=key;
    if (!key) return NULL;
    const char *s=name;
    while (*s) {
        size_t bytes;
        unsigned int cp=Pinyin_decode((const unsigned char *)s,&bytes);
        const char *reading=NULL;
        if (cp>=0x3400) {
            /* Longest dictionary phrase wins over the default character reading. */
            size_t n=bytes;
            while (s[n] && n<PINYIN_MAX_PHRASE_BYTES) {
                size_t next;
                unsigned int following=Pinyin_decode((const unsigned char *)s+n,&next);
                if (following<0x3400 || next==1) break;
                n+=next;
                const char *phrase=Pinyin_phrase(s,n);
                if (phrase) { reading=phrase; bytes=n; }
            }
            if (!reading) reading=Pinyin_char(cp);
        }
        if (reading) { size_t n=strlen(reading); memcpy(out,reading,n); out+=n; }
        else if (bytes==1 && *s>='A' && *s<='Z') *out++=*s-'A'+'a';
        else { memcpy(out,s,bytes); out+=bytes; }
        s+=bytes;
    }
    *out=0;
    return key;
}
static int Pinyin_initial(const char *key) {
    unsigned char c=(unsigned char)key[0];
    if (c>='A' && c<='Z') c+= 'a'-'A';
    return c>='a' && c<='z' ? c-'a'+1 : 0;
}
#endif
