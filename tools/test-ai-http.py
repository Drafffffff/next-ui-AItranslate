#!/usr/bin/env python3
"""Exercise the actual C request functions with a synthetic curl, offline."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
output = root / "build/pocketjs-port"
sdk = Path("/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc")
qjs = output / "quickjs/libquickjs-sys/embed/quickjs"

with tempfile.TemporaryDirectory(prefix="ai-http-") as temp:
    temp = Path(temp)
    fake = temp / "curl"
    fake.write_text('''#!/usr/bin/python3
import os,sys
args=sys.argv[1:]
config=args[args.index('--config')+1]
text=sys.stdin.read() if config=='-' else open(config).read()
expected='header = "Authorization: Bearer '+os.environ['EXPECTED_AUTH']+'"'
if expected not in text:print('synthetic header mismatch',file=sys.stderr);sys.exit(42)
if os.environ.get('CHECK_REDIRECT')=='1':
 indices=[i for i,x in enumerate(args) if x=='--max-redirs']
 if not indices or args[indices[-1]+1]!='0':sys.exit(43)
if '--write-out' not in args and '-w' not in args:sys.exit(44)
if '--data-binary' not in args: # native HTTP test has no body
 print('{}\\n200',end='')
else:
 out=args[args.index('-o')+1]
 open(out,'w').write('{}')
 print('200 0 0 0 0 0 0 0 0 0',end='')
''')
    fake.chmod(0o755)
    fixture = temp / "ai-keys.txt"
    environment = dict(os.environ, PATH=str(temp)+":"+os.environ["PATH"], NEXTUI_AI_KEYS_FILE=str(fixture))

    def run(name, source, includes=()):
        file = temp / (name+".c")
        file.write_text(source)
        binary = temp / name
        flags=["-std=gnu11","-O1","-ffunction-sections","-fdata-sections","-Wl,--gc-sections","-Wno-unused-function"]
        subprocess.run(["aarch64-nextui-linux-gnu-gcc",*flags,*includes,str(file),"-lpthread","-lm","-o",str(binary)],check=True)
        subprocess.run([str(sdk/"lib/ld-linux-aarch64.so.1"),"--library-path",str(sdk/"lib")+":"+str(sdk/"usr/lib"),str(binary)],check=True,env=environment)

    native = f'#include "{root / "ports/pocketjs-brick/brick-services.c"}"\n#include <assert.h>\n'
    native += r'''
static void fixture(const char* text){FILE* f=fopen(getenv("NEXTUI_AI_KEYS_FILE"),"w");assert(f);fputs(text,f);fclose(f);}
static void request_check(const char* url,const char* expected,int redir){
 setenv("EXPECTED_AUTH",expected,1);setenv("CHECK_REDIRECT",redir?"1":"0",1);
 Job j={0};snprintf(j.url,sizeof(j.url),"%s",url);strcpy(j.method,"POST");strcpy(j.auth,"legacy-fake");j.timeout_ms=2000;
 http(&j);assert(!j.error[0]&&j.http_status==200);release(&j);
}
int main(void){
 fixture("DEEPSEEK_API_KEY=shared-fake\nDASHSCOPE_API_KEY=asr-fake\n");
 request_check("https://api.deepseek.com/chat/completions","shared-fake",1);
 request_check("https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions","asr-fake",1);
 request_check("http://127.0.0.1:12345/chat/completions","legacy-fake",0);
 fixture("DEEPSEEK_API_KEY=changed-fake\n");request_check("https://api.deepseek.com/chat/completions","changed-fake",1);
 fixture("DEEPSEEK_API_KEY=\n");request_check("https://api.deepseek.com/chat/completions","legacy-fake",0);
 fixture("DEEPSEEK_API_KEY=one\nDEEPSEEK_API_KEY=two\n");
 Job j={0};strcpy(j.url,"https://api.deepseek.com/chat/completions");strcpy(j.auth,"legacy-fake");http(&j);assert(j.error[0]&&!j.pid);release(&j);
 puts("PASS: native HTTP shared priority, provider isolation, reload, fallback, invalid config, secret-free argv and redirect policy");
}
'''
    include_flags=[]
    for path in [output/"upstream/engine/ui-cabi/include",output/"upstream/engine/quickjs-c",output/"upstream/hosts/nokia-e7/runtime",output/"upstream/contracts/generated",qjs]:
        include_flags.append("-I"+str(path))
    include_flags += subprocess.check_output(["pkg-config","--cflags","sdl2"],text=True).split()
    run("native-http",native,include_flags)

    ai=(root/"workspace/all/minarch/ma_ai.c").read_text()
    functions=ai[ai.index("static int ai_have_cmd("):ai.index("/* ------------------------------------------------------------------ JSON 解析 */")]
    code=f'#include "{root / "workspace/all/common/ai_credentials.h"}"\n#include <assert.h>\n#include <unistd.h>\n#include <fcntl.h>\n'
    code += f'#define AI_TMP_DIR "{temp}"\n#define AI_RESP_PATH "{temp / "response.json"}"\n'
    code += r'''
static double ai_net_dns,ai_net_conn,ai_net_tls,ai_net_pre,ai_net_first,ai_net_total,ai_net_speed;
static long ai_net_up,ai_net_down;
static unsigned char* ai_read_file(const char* p,size_t* n){FILE* f=fopen(p,"rb");if(!f)return NULL;unsigned char* s=calloc(1,1024);*n=fread(s,1,1023,f);fclose(f);return s;}
#define AI_PROVIDER_DEEPSEEK 1
#define AI_PROVIDER_BAILIAN 0
#define AI_PROVIDER_CUSTOM 2
static int ai_c_provider = AI_PROVIDER_DEEPSEEK, ai_c_enable = 1;
static char ai_c_dkey[192] = "legacy-fake", ai_c_bkey[192] = "legacy-asr-fake", ai_c_key[192] = "legacy-generic-fake";
static void ai_cfg_load(void) {}
'''+ai[ai.index('static const char* ai_key(void) {'):ai.index('/* ------------------------------------------------------------------ 错误提示 */')]+functions
    code+=r'''
int main(void){
 FILE* f=fopen(getenv("NEXTUI_AI_KEYS_FILE"),"w");assert(f);fputs("DEEPSEEK_API_KEY='$(never-executed)'\"\\literal\n",f);fclose(f);
 setenv("EXPECTED_AUTH","'$(never-executed)'\\\"\\\\literal",1);setenv("CHECK_REDIRECT","1",1);
 char body[] = AI_TMP_DIR "/body.json",error[192];f=fopen(body,"w");assert(f);fputs("{}",f);fclose(f);
 int status=ai_http_post("https://api.deepseek.com/chat/completions",body,2,error,sizeof(error));
 if(status!=200)fprintf(stderr,"translation fixture: %d %s\n",status,error);
 assert(status==200);
 puts("PASS: actual translation HTTP literal key escaping, inherited anonymous descriptor, no credential in shell or argv");
}
'''
    run("translation-http",code)
