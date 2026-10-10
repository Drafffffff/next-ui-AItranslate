/* Capability-limited JavaScript terminal. No OS, filesystem or network bindings.
 * The handheld kernel has no seccomp/namespaces, so never fall back to a shell. */
#define _GNU_SOURCE
#include "quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <grp.h>
#endif
#define INPUT_LIMIT (8*1024*1024+65536)
#define OUTPUT_LIMIT 16384
static char output[OUTPUT_LIMIT+1];static size_t output_size;static int truncated;
static double deadline;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int interrupt(JSRuntime *rt,void *opaque){(void)rt;(void)opaque;return now()>deadline;}
static void append(const char *s,size_t length){size_t take=length;if(take>OUTPUT_LIMIT-output_size){take=OUTPUT_LIMIT-output_size;truncated=1;}memcpy(output+output_size,s,take);output_size+=take;}
static JSValue print(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;for(int i=0;i<argc;i++){size_t n;const char *s=JS_ToCStringLen(ctx,&n,argv[i]);if(!s)return JS_EXCEPTION;if(i)append(" ",1);append(s,n);JS_FreeCString(ctx,s);}append("\n",1);return JS_UNDEFINED;}
static void exception(JSContext *ctx){JSValue e=JS_GetException(ctx);const char *s=JS_ToCString(ctx,e);if(s){append(s,strlen(s));JS_FreeCString(ctx,s);}JS_FreeValue(ctx,e);append("\n",1);}
int main(void){
 struct rlimit cpu={4,4},files={1024*1024,1024*1024},core={0,0},fds={16,16};
 if(setrlimit(RLIMIT_CPU,&cpu)||setrlimit(RLIMIT_FSIZE,&files)||setrlimit(RLIMIT_CORE,&core)||setrlimit(RLIMIT_NOFILE,&fds))return 70;
#ifdef __linux__
 /* Fail closed if privilege dropping is unavailable; no inherited app handles. */
 if(geteuid()==0&&(setgroups(0,NULL)||setgid(65534)||setuid(65534)))return 70;
 if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))return 70;
#endif
 for(int fd=3;fd<1024;fd++)close(fd);

#ifdef __linux__
 clearenv();
#else
 extern char **environ;static char *empty_environment[]={NULL};environ=empty_environment;
#endif
 char *input=malloc(INPUT_LIMIT+1);if(!input)return 70;
 size_t n=fread(input,1,INPUT_LIMIT,stdin);if(ferror(stdin)||!n||fgetc(stdin)!=EOF){free(input);return 64;}input[n]=0;
 JSRuntime *rt=JS_NewRuntime();if(!rt){free(input);return 70;}
 JS_SetMemoryLimit(rt,64*1024*1024);JS_SetMaxStackSize(rt,256*1024);deadline=now()+3;JS_SetInterruptHandler(rt,interrupt,NULL);
 JSContext *ctx=JS_NewContext(rt);if(!ctx){free(input);JS_FreeRuntime(rt);return 70;}
 JSValue request=JS_ParseJSON(ctx,input,n,"terminal-input");free(input);
 JSValue source=JS_GetPropertyStr(ctx,request,"lua"),script=JS_GetPropertyStr(ctx,request,"script");size_t length;
 const char *code=JS_ToCStringLen(ctx,&length,script);
 int result=0;
 if(JS_IsException(request)||!JS_IsString(source)||!JS_IsString(script)||!code||length>16384){result=64;goto done;}
 JSValue global=JS_GetGlobalObject(ctx);
 JSValue snapshots=JS_GetPropertyStr(ctx,request,"files");JS_SetPropertyStr(ctx,global,"__files",snapshots);
 JS_SetPropertyStr(ctx,global,"__source",JS_DupValue(ctx,source));
 JS_SetPropertyStr(ctx,global,"print",JS_NewCFunction(ctx,print,"print",1));JS_FreeValue(ctx,global);
 const char *bootstrap="(()=>{const files=new Map([['game.lua',__source],...Object.entries(__files||{})]);delete globalThis.__source;delete globalThis.__files;const name=n=>{if(typeof n!=='string'||!/^[a-zA-Z0-9_-]{1,64}\\.(lua|js|txt|json|csv)$/.test(n))throw Error('仅支持临时工作区的普通文件名');return n};globalThis.readText=n=>{if(typeof n!=='string'||!files.has(n))throw Error('临时文件不存在');return files.get(n)};globalThis.writeText=(n,s)=>{n=name(n);if(typeof s!=='string'||s.length>1048576||files.size>=16&&!files.has(n))throw Error('临时文件超过预算');files.set(n,s)};globalThis.listFiles=()=>Array.from(files.keys());globalThis.console=Object.freeze({log:print,error:print,warn:print});})();";
 JSValue boot=JS_Eval(ctx,bootstrap,strlen(bootstrap),"terminal-workspace",JS_EVAL_TYPE_GLOBAL);
 if(JS_IsException(boot)){JS_FreeValue(ctx,boot);result=70;exception(ctx);goto done;}JS_FreeValue(ctx,boot);
 JSValue value=JS_Eval(ctx,code,length,"terminal-script.js",JS_EVAL_TYPE_GLOBAL);
 if(JS_IsException(value)){result=1;exception(ctx);}JS_FreeValue(ctx,value);
 if(JS_IsJobPending(rt)&&!result){append("异步任务未执行；请使用同步辅助脚本\n",strlen("异步任务未执行；请使用同步辅助脚本\n"));result=1;}
 done:
 if(code)JS_FreeCString(ctx,code);
 JS_FreeValue(ctx,source);JS_FreeValue(ctx,script);JS_FreeValue(ctx,request);JS_FreeContext(ctx);JS_FreeRuntime(rt);
 fwrite(output,1,output_size,stdout);if(truncated)fputs("\n[输出已截断，最多16KiB]\n",stdout);return result;
}
