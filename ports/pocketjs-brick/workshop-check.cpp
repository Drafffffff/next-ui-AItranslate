/* Parse only. Never execute generated Lua or initialize game APIs. */
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include "lua.h"
#include "lauxlib.h"
static size_t allocated;
static constexpr size_t source_budget=8*1024*1024;
static void *bounded(void*,void *ptr,size_t old,size_t size){
 if(!ptr)old=0;
 if(!size){std::free(ptr);allocated-=old;return nullptr;}
 if(size>32*1024*1024||allocated-old>32*1024*1024-size)return nullptr;
 void *next=std::realloc(ptr,size);if(next)allocated=allocated-old+size;return next;
}
int main(){
 alarm(3);size_t length=0,capacity=4096;char *text=(char*)std::malloc(capacity);
 if(!text)return 2;
 for(;;){if(length==capacity){if(capacity>=source_budget){if(std::fgetc(stdin)==EOF&&!std::ferror(stdin))break;std::puts("Lua source exceeds parser byte budget");std::free(text);return 2;}capacity*=2;char *next=(char*)std::realloc(text,capacity);if(!next){std::free(text);return 2;}text=next;}
  size_t n=std::fread(text+length,1,capacity-length,stdin);length+=n;if(!n){if(std::ferror(stdin)){std::free(text);return 2;}break;}}
 lua_State *state=lua_newstate(bounded,nullptr);if(!state){std::free(text);return 2;}
 int code=luaL_loadbuffer(state,text,length,"game");
 if(code)std::printf("%.800s\n",lua_tostring(state,-1));
 else std::puts("syntax_checked");
 lua_close(state);std::free(text);return code?1:0;
}
