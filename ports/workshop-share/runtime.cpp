// Web adapter for the same pinned FAKE-08 and z8lua used on the handheld.
#include <string>
#include <cstdint>
#include <memory>
#include "vm.h"
#include "stubhost.h"
#include "nibblehelpers.h"
#include <emscripten.h>
static std::unique_ptr<StubHost> host;
static std::unique_ptr<Vm> vm;
static uint8_t previous=0;
static int fps=30;
static uint8_t pixels[128*128*4];
static int16_t pcm[1470];
extern "C" {
EMSCRIPTEN_KEEPALIVE int web_load(const unsigned char* cart,int size){
 vm.reset();host=std::make_unique<StubHost>();host->setUpPaletteColors();vm=std::make_unique<Vm>(host.get());previous=0;host->stubInput(0,0);
 if(!vm->LoadCart(cart,size,false))return 0;
 vm->vm_run();if(!vm->Step())return 0;
 fps=vm->ExecuteLua("function __workshop_rate() return type(_update60)=='function' end","__workshop_rate")?60:30;
 return fps;
}
EMSCRIPTEN_KEEPALIVE int web_step(int mask){
 if(!vm)return 0;
 host->stubInput(mask&~previous,mask);previous=mask;
 for(int i=0;i<(fps==30?2:1);i++)if(!vm->Step())return 0;
 auto palette=host->GetPaletteColors();auto map=vm->GetScreenPaletteMap();auto fb=vm->GetPicoInteralFb();
 for(int y=0;y<128;y++)for(int x=0;x<128;x++){auto c=palette[map[getPixelNibble(x,y,fb)]%144];int i=(y*128+x)*4;pixels[i]=c.Red;pixels[i+1]=c.Green;pixels[i+2]=c.Blue;pixels[i+3]=255;}
 vm->FillAudioBuffer(pcm,0,22050/fps);
 return 22050/fps;
}
EMSCRIPTEN_KEEPALIVE uint8_t* web_pixels(){return pixels;}
EMSCRIPTEN_KEEPALIVE int16_t* web_audio(){return pcm;}
EMSCRIPTEN_KEEPALIVE const char* web_error(){static std::string error;error=vm?vm->GetBiosError():"No cartridge";return error.c_str();}
}
