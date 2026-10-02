#!/usr/bin/env python3
"""Test the real native sleep worker against an isolated Unix-socket daemon.

Uses the existing PocketJS headers, but dead-strips unused UI/QuickJS code.
No production backend, Bluetooth stack, or app is started.
"""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CORE = ROOT / 'build/pocketjs-port'
NATIVE = ROOT / 'ports/brick-mic/brick/mic-native.c'

HARNESS = r'''
#include <assert.h>
#include "NATIVE_SOURCE"
static pthread_mutex_t test_mutex=PTHREAD_MUTEX_INITIALIZER;
static int listener,test_connected=1,status_count,prepare_count,abort_count,prepare_allowed=1,test_stopping;
static void *server(void *unused){
    (void)unused;
    for(;;){
        int fd=accept(listener,NULL,NULL);if(fd<0)break;
        char line[100]={0},reply[100];int n=(int)recv(fd,line,sizeof(line)-1,0);if(n<=0){close(fd);continue;}
        pthread_mutex_lock(&test_mutex);
        if(!strcmp(line,"ping\n"))snprintf(reply,sizeof(reply),"ready\n");
        else if(!strcmp(line,"prepare-deep-sleep\n")){prepare_count++;snprintf(reply,sizeof(reply),"%s\n",prepare_allowed&&!test_connected?"asleep-ready":"blocked");}
        else if(!strcmp(line,"abort-deep-sleep\n")){abort_count++;snprintf(reply,sizeof(reply),"ok\n");}
        else{if(!strcmp(line,"status\n"))status_count++;snprintf(reply,sizeof(reply),"{\"state\":\"ready\",\"connected\":%s}\n",test_connected?"true":"false");}
        int done=test_stopping;pthread_mutex_unlock(&test_mutex);
        send(fd,reply,strlen(reply),0);close(fd);if(done)break;
    }
    return NULL;
}
static void set_connection(int value){pthread_mutex_lock(&test_mutex);test_connected=value;pthread_mutex_unlock(&test_mutex);}
static int count(int *value){pthread_mutex_lock(&test_mutex);int n=*value;pthread_mutex_unlock(&test_mutex);return n;}
static void wait_connection(int value){
    double until=now()+2.5;while(now()<until&&brick_mic_connection()!=value)usleep(10000);assert(brick_mic_connection()==value);
}
int main(int argc,char **argv){
    assert(argc==2);signal(SIGPIPE,SIG_IGN);
    assert(read_connection("{\"connected\":false,\"control\":{\"connected\":true}}") == 0);
    assert(read_connection("{\"message\":\"connected\",\"connected\": true}") == 1);
    assert(read_connection("{\"message\":\"\\\"connected\\\"\",\"connected\": false}") == 0);
    assert(read_connection("{\"connected\":null}") == -1);
    puts("PASS: only a top-level connection boolean can control the sleep timer");
    snprintf(socket_path,sizeof(socket_path),"%s",argv[1]);
    listener=socket(AF_UNIX,SOCK_STREAM,0);assert(listener>=0);
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;snprintf(address.sun_path,sizeof(address.sun_path),"%s",socket_path);
    assert(bind(listener,(struct sockaddr*)&address,sizeof(address))==0&&listen(listener,8)==0);
    pthread_t test_server;assert(pthread_create(&test_server,NULL,server,NULL)==0);
    preview=1;presented=1;worker_started=1;assert(pthread_create(&worker,NULL,run,NULL)==0);
    wait_connection(1);brick_mic_sleep(1,0);set_connection(0);wait_connection(0);
    puts("PASS: screen-off worker still detects disconnect without waking or rendering");
    assert(brick_mic_prepare_deep_sleep()==1&&count(&prepare_count)==1);
    int before=count(&status_count);usleep(1200000);assert(count(&status_count)==before);
    puts("PASS: final preparation freezes status RPC before platform suspend");
    brick_mic_abort_deep_sleep();assert(count(&abort_count)==1);set_connection(1);wait_connection(1);
    assert(brick_mic_prepare_deep_sleep()==0&&count(&prepare_count)==1);
    puts("PASS: abort resumes polling; live reconnect prevents preparation");
    set_connection(0);wait_connection(0);
    pthread_mutex_lock(&test_mutex);prepare_allowed=0;pthread_mutex_unlock(&test_mutex);
    assert(brick_mic_prepare_deep_sleep()==0&&count(&prepare_count)==2&&count(&abort_count)==2);
    before=count(&status_count);usleep(1200000);assert(count(&status_count)>before);
    puts("PASS: daemon reservation rejection unfreezes the worker");
    brick_mic_sleep(0,0);set_connection(1);wait_connection(1);
    pthread_mutex_lock(&mutex);stopping=1;pthread_cond_broadcast(&wake);pthread_mutex_unlock(&mutex);pthread_join(worker,NULL);
    pthread_mutex_lock(&test_mutex);test_stopping=1;pthread_mutex_unlock(&test_mutex);
    char out[100];rpc("status",out,sizeof(out));pthread_join(test_server,NULL);close(listener);unlink(socket_path);
    puts("PASS: fast wake and shutdown complete without a suspended worker deadlock");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='brick-mic-worker-check-') as folder:
    root = Path(folder)
    (root / 'check.c').write_text(HARNESS.replace('NATIVE_SOURCE', str(NATIVE)))
    flags = subprocess.check_output(['pkg-config', '--cflags', 'sdl2', 'SDL2_ttf'], text=True).split()
    includes = [CORE / 'upstream/engine/ui-cabi/include', CORE / 'upstream/hosts/nokia-e7/runtime',
                CORE / 'quickjs/libquickjs-sys/embed/quickjs', ROOT / 'ports/pocketjs-brick']
    compiler = os.environ.get('CC', 'clang')
    subprocess.run([compiler, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-ffunction-sections', '-fdata-sections', '-Wl,-dead_strip',
                    *['-I' + str(p) for p in includes], *flags, str(root / 'check.c'),
                    '-o', str(root / 'check')], check=True)
    subprocess.run([str(root / 'check'), str(root / 'mic.sock')], check=True, timeout=15)
