// Persistent uinput keyboard. Works on immutable Bazzite without Python headers.
#include <linux/uinput.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static int emit(int fd,unsigned short type,unsigned short code,int value){
    struct input_event ev={0};ev.type=type;ev.code=code;ev.value=value;
    return write(fd,&ev,sizeof(ev))==(ssize_t)sizeof(ev);
}
int main(void){
    setvbuf(stdout,NULL,_IOLBF,0);
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK);
    if(fd<0||ioctl(fd,UI_SET_EVBIT,EV_KEY)<0||ioctl(fd,UI_SET_EVBIT,EV_SYN)<0)return 1;
    // Advertise only the keys we actually send. Exposing POWER/SLEEP or
    // every scan code makes handheld daemons classify/grab this as a power
    // button/controller device instead of a normal text keyboard.
    for(int i=KEY_ESC;i<=KEY_KPDOT;i++)if(ioctl(fd,UI_SET_KEYBIT,i)<0)return 1;
    const int keys[]={KEY_LEFT,KEY_RIGHT,KEY_UP,KEY_DOWN};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)if(ioctl(fd,UI_SET_KEYBIT,keys[i])<0)return 1;
    struct uinput_setup dev={0};dev.id.bustype=BUS_USB;dev.id.vendor=0x1209;dev.id.product=0xba1c;
    snprintf(dev.name,sizeof(dev.name),"Brick Mic Keyboard");
    if(ioctl(fd,UI_DEV_SETUP,&dev)<0||ioctl(fd,UI_DEV_CREATE)<0)return 1;
    usleep(300000);puts("ready");
    char line[128];
    while(fgets(line,sizeof(line),stdin)){
        unsigned short keys[4];int n=0,ok=1;char *p=line,*end;
        while(*p&&*p!='\n'){
            long key=strtol(p,&end,10);if(end==p||key<1||key>=256||n==4){ok=0;break;}
            keys[n++]=(unsigned short)key;p=end;while(*p==' ')p++;
        }
        if(!ok||!n){puts("error");continue;}
        for(int i=0;i<n;i++)ok=emit(fd,EV_KEY,keys[i],1)&&ok;
        ok=emit(fd,EV_SYN,SYN_REPORT,0)&&ok;usleep(15000);
        for(int i=n-1;i>=0;i--)ok=emit(fd,EV_KEY,keys[i],0)&&ok;
        ok=emit(fd,EV_SYN,SYN_REPORT,0)&&ok;puts(ok?"ok":"error");
    }
    ioctl(fd,UI_DEV_DESTROY);close(fd);return 0;
}
